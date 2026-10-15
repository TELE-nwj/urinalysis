"""只读整盘备份。仅对已识别的4GB General UDisk建镜像，不包含介质写入接口。"""
import ctypes
from ctypes import wintypes as w
from pathlib import Path
import hashlib, json, struct, time, os

root = Path(__file__).resolve().parent
expected_size = 4026499072
path = r'\\.\H:'
kernel = ctypes.WinDLL('kernel32', use_last_error=True)
kernel.CreateFileW.argtypes = [w.LPCWSTR,w.DWORD,w.DWORD,ctypes.c_void_p,w.DWORD,w.DWORD,w.HANDLE]
kernel.CreateFileW.restype = w.HANDLE
kernel.ReadFile.argtypes = [w.HANDLE,ctypes.c_void_p,w.DWORD,ctypes.POINTER(w.DWORD),ctypes.c_void_p]
kernel.SetFilePointerEx.argtypes = [w.HANDLE,ctypes.c_longlong,ctypes.POINTER(ctypes.c_longlong),w.DWORD]
kernel.DeviceIoControl.argtypes = [w.HANDLE,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.c_void_p,w.DWORD,ctypes.POINTER(w.DWORD),ctypes.c_void_p]
kernel.CloseHandle.argtypes = [w.HANDLE]

target = root / 'General-UDisk-H-partition-before-repair.img'
if target.exists(): raise RuntimeError('完整镜像已存在，不能覆盖')
handle = kernel.CreateFileW(path,0x80000000,3,None,3,0,None)
if handle == ctypes.c_void_p(-1).value: raise ctypes.WinError(ctypes.get_last_error())

chunk = 4 * 1024 * 1024
buf = ctypes.create_string_buffer(chunk)
def read(amount):
    received = w.DWORD()
    if not kernel.ReadFile(handle,buf,amount,ctypes.byref(received),None):
        raise ctypes.WinError(ctypes.get_last_error())
    if received.value != amount: raise RuntimeError(f'短读：{received.value}/{amount}')
    return buf.raw[:amount]
def seek(offset):
    pos = ctypes.c_longlong()
    if not kernel.SetFilePointerEx(handle,offset,ctypes.byref(pos),0): raise ctypes.WinError(ctypes.get_last_error())

try:
    size = ctypes.c_longlong(); returned = w.DWORD()
    if not kernel.DeviceIoControl(handle,0x7405c,None,0,ctypes.byref(size),8,ctypes.byref(returned),None):
        raise ctypes.WinError(ctypes.get_last_error())
    if size.value != expected_size: raise RuntimeError(f'目标磁盘容量变化：{size.value}')
    start = 64
    boot = read(512)
    if boot[510:512] != b'\x55\xaa' or struct.unpack_from('<H',boot,11)[0] != 512 or struct.unpack_from('<H',boot,14)[0] != 1056:
        raise RuntimeError('BPB与已确认目标不匹配')
    (root/'boot-before.bin').write_bytes(boot)
    seek(0)
    digest = hashlib.sha256(); total = 0; began = time.monotonic(); last = began
    with target.open('xb') as output:
        while total < expected_size:
            data = read(min(chunk,expected_size-total))
            output.write(data); digest.update(data); total += len(data)
            now = time.monotonic()
            if now-last >= 15:
                print(f'BACKUP {total}/{expected_size} ({total/expected_size:.1%}) {total/(now-began)/1048576:.1f} MiB/s',flush=True)
                last=now
        output.flush(); os.fsync(output.fileno())
    if target.stat().st_size != expected_size: raise RuntimeError('镜像长度不符')
    metadata = {'source':path,'mode':'GENERIC_READ only','scope':'complete H: partition; MBR excluded because PhysicalDrive access denied','bytes':total,'sha256_stream':digest.hexdigest(),'partition_sector':start,'duration_seconds':round(time.monotonic()-began,2),'completed':True}
    (root/'backup-manifest.json').write_text(json.dumps(metadata,indent=2),encoding='utf8')
    print(json.dumps(metadata),flush=True)
finally:
    kernel.CloseHandle(handle)
