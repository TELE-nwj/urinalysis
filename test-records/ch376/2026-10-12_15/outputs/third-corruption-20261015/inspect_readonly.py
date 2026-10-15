"""只读保存已识别H卷的FAT/目录/相关扇区，禁止任何介质写入。"""
from pathlib import Path
import ctypes
from ctypes import wintypes as w
import hashlib
import json
import re
import shutil
import struct
import subprocess

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[1]
AUDIT = OUT / 'disk-audit'
AUDIT.mkdir(exist_ok=True)
log_source = Path('G:/新建文件夹/串口日志_20261015_174855.log')
shutil.copy2(log_source, OUT / log_source.name)

# 在打开原始卷前重新核对物理磁盘与分区，绝不按历史盘符盲读。
command = "$p=Get-Partition -DriveLetter H; $d=Get-Disk -Number $p.DiskNumber; [pscustomobject]@{DiskNumber=$d.Number;FriendlyName=$d.FriendlyName;BusType=[string]$d.BusType;DiskSize=$d.Size;Offset=$p.Offset;PartitionSize=$p.Size} | ConvertTo-Json -Compress"
identity = json.loads(subprocess.check_output(['powershell', '-NoProfile', '-Command', command], text=True))
assert identity['FriendlyName'] == 'General UDisk' and identity['BusType'] == 'USB'
assert (identity['DiskNumber'], identity['DiskSize'], identity['Offset'], identity['PartitionSize']) == (1, 4026531840, 32768, 4026499072)
(AUDIT / 'identity.json').write_text(json.dumps(identity, indent=2), encoding='utf8')

kernel = ctypes.WinDLL('kernel32', use_last_error=True)
kernel.CreateFileW.argtypes = [w.LPCWSTR,w.DWORD,w.DWORD,ctypes.c_void_p,w.DWORD,w.DWORD,w.HANDLE]
kernel.CreateFileW.restype = w.HANDLE
kernel.ReadFile.argtypes = [w.HANDLE,ctypes.c_void_p,w.DWORD,ctypes.POINTER(w.DWORD),ctypes.c_void_p]
kernel.SetFilePointerEx.argtypes = [w.HANDLE,ctypes.c_longlong,ctypes.POINTER(ctypes.c_longlong),w.DWORD]
kernel.DeviceIoControl.argtypes = [w.HANDLE,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.POINTER(w.DWORD),ctypes.c_void_p]
kernel.CloseHandle.argtypes = [w.HANDLE]
handle = kernel.CreateFileW(r'\\.\H:', 0x80000000, 3, None, 3, 0, None)
if handle == ctypes.c_void_p(-1).value:
    raise ctypes.WinError(ctypes.get_last_error())

def read_at(offset, size):
    assert offset >= 0 and size > 0 and offset + size <= identity['PartitionSize']
    position = ctypes.c_longlong()
    if not kernel.SetFilePointerEx(handle, offset, ctypes.byref(position), 0):
        raise ctypes.WinError(ctypes.get_last_error())
    buffer = ctypes.create_string_buffer(size)
    received = w.DWORD()
    if not kernel.ReadFile(handle, buffer, size, ctypes.byref(received), None):
        raise ctypes.WinError(ctypes.get_last_error())
    assert received.value == size, (received.value, size)
    return buffer.raw

result = {'identity': identity, 'access': 'GENERIC_READ only', 'files': []}
try:
    actual_size, returned = ctypes.c_longlong(), w.DWORD()
    if not kernel.DeviceIoControl(handle, 0x7405c, None, 0, ctypes.byref(actual_size), 8, ctypes.byref(returned), None):
        raise ctypes.WinError(ctypes.get_last_error())
    assert actual_size.value == identity['PartitionSize']
    boot = read_at(0, 512)
    bps, reserved = struct.unpack_from('<H',boot,11)[0], struct.unpack_from('<H',boot,14)[0]
    spc, copies = boot[13], boot[16]
    fat_sectors, root_cluster = struct.unpack_from('<I',boot,36)[0], struct.unpack_from('<I',boot,44)[0]
    sectors = struct.unpack_from('<I',boot,32)[0]
    assert boot[510:512] == b'\x55\xaa'
    assert (bps, spc, reserved, copies, fat_sectors, root_cluster) == (512,8,1056,2,7664,2)
    (AUDIT/'boot-sector.bin').write_bytes(boot)
    data_sector = reserved + copies * fat_sectors
    clusters = (sectors-data_sector)//spc
    fats = [read_at((reserved + i*fat_sectors)*bps, fat_sectors*bps) for i in range(copies)]
    for i,fat in enumerate(fats):
        (AUDIT/f'fat-{i+1}.bin').write_bytes(fat)
    differences = [i for i in range(fat_sectors) if fats[0][i*bps:(i+1)*bps] != fats[1][i*bps:(i+1)*bps]]
    result.update({'data_volume_sector': data_sector, 'data_absolute_lba': data_sector+64,
                   'fat_first_entries': [[struct.unpack_from('<I',f,i*4)[0] for i in range(16)] for f in fats],
                   'fat_mismatch_sectors': differences})
    def fat_entry(cluster):
        return struct.unpack_from('<I', fats[0], cluster*4)[0] & 0x0fffffff
    def cluster_bytes(cluster):
        assert 2 <= cluster < clusters+2
        data = read_at((data_sector+(cluster-2)*spc)*bps,spc*bps)
        (AUDIT/f'cluster-{cluster}.bin').write_bytes(data)
        return data
    # 有界遍历根目录簇链，不把非法的0/1簇换算为文件数据地址。
    root_data, current, seen = b'', root_cluster, set()
    while 2 <= current < clusters+2 and current not in seen and len(seen)<32:
        seen.add(current);root_data += cluster_bytes(current);current=fat_entry(current)
    result['root_chain'] = sorted(seen)
    for at in range(0,len(root_data),32):
        entry=root_data[at:at+32]
        if entry[0]==0:break
        if entry[0]==0xe5 or entry[11]==0x0f:continue
        if not re.fullmatch(rb'U[0-9]{7}TXT',entry[:11]):continue
        name=entry[:8].decode()+'.TXT'
        cluster=(struct.unpack_from('<H',entry,20)[0]<<16)|struct.unpack_from('<H',entry,26)[0]
        size=struct.unpack_from('<I',entry,28)[0]
        row={'name':name,'root_byte_offset':at,'entry_hex':entry.hex(' '),'cluster':cluster,'bytes':size,
             'valid_start_cluster':size==0 or 2<=cluster<clusters+2}
        if 2<=cluster<clusters+2:
            row['first_cluster_lba']=data_sector+64+(cluster-2)*spc
            data=cluster_bytes(cluster)
            row['first_cluster_utf8_excerpt']=data[:min(size,256)].decode('utf8',errors='replace')
            row['fat_link']=fat_entry(cluster)
        result['files'].append(row)
    # 日志本次写到绝对LBA16432，对应卷16368，在第二份FAT末尾。
    suspect=read_at((16432-64)*bps,spc*bps)
    (AUDIT/'absolute-lba-16432.bin').write_bytes(suspect)
    result['suspect_lba16432_header_offset']=suspect.find('时间(hh:mm:ss)'.encode('utf8'))
    result['suspect_lba16432_first_96_hex']=suspect[:96].hex(' ')
    result['suspect_lba16432_excerpt']=suspect[:96].decode('utf8',errors='replace')
    previous=ROOT/'outputs/third-acceptance-20261014/disk-audit'
    comparisons=[]
    for i,old_name in enumerate(['fat-first.bin','fat-second.bin']):
        old=(previous/old_name).read_bytes()
        delta=[s for s in range(fat_sectors) if old[s*bps:(s+1)*bps]!=fats[i][s*bps:(s+1)*bps]]
        comparisons.append({'copy':i+1,'changed_sectors_since_B_snapshot':delta,'old_first_16_hex':old[:16].hex(' '),'new_first_16_hex':fats[i][:16].hex(' ')})
    result['comparison_with_B'] = comparisons
finally:
    kernel.CloseHandle(handle)

for row in result['files']:
    try:
        data=Path('H:/',row['name']).read_bytes()
        (OUT/row['name']).write_bytes(data)
        row.update({'windows_read':'success','read_bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),
                    'text':data.decode('utf8',errors='replace')})
    except OSError as error:
        row.update({'windows_read':'failed','error':str(error),'winerror':error.winerror})
log=(OUT/log_source.name).read_text(encoding='utf-8-sig')
lines=log.splitlines()
result['log_summary']={
    'locked_files':re.findall(r'锁定文件=(\S+)；',log),
    'create_commands':len(re.findall(r'命令=0x34\(创建文件\)',log)),
    'header_write_commands':len(re.findall(r'命令=0x3C\(请求字节写入\)',log)),
    'new_file_ready':len(re.findall(r'storage: 新测量文件:',log)),
    'minutes_skipped':re.findall(r'第(\d+)分钟未存储',log),
    'monitor_transitions':re.findall(r'\((\d+)\) monitor: 状态 (\S+) -> (\S+)',log),
    'recovery_failures':len(re.findall('恢复会话失败',log)),
    'new_creations_in_recovery':len(re.findall(r'文件不存在，现场创建|已创建测量文件并写入表头',log)),
    'suspicious_write': [line for line in lines if '字节写完成、关闭前' in line],
}
(OUT/'inspection.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(result,ensure_ascii=False,indent=2))
