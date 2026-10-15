"""从备份镜像提取已识别的误写文本，仅写电脑输出目录。"""
from pathlib import Path
import json, re, struct

root=Path(__file__).resolve().parent
image=root/'General-UDisk-H-partition-before-repair.img'
recovered=root/'recovered-records'
recovered.mkdir(exist_ok=True)
with image.open('rb') as source:
    boot=source.read(512)
    bps=struct.unpack_from('<H',boot,11)[0]
    spc=boot[13]; reserved=struct.unpack_from('<H',boot,14)[0]
    fatlen=struct.unpack_from('<I',boot,36)[0]
    correct_base=reserved+boot[16]*fatlen
    suspected_base=correct_base-1024
    source.seek(suspected_base*bps)
    suspect_dir=source.read(spc*bps)
    mapped={}
    for at in range(0,len(suspect_dir),32):
        entry=suspect_dir[at:at+32]
        if not re.fullmatch(rb'U[0-9]{7}TXT',entry[:11]) or entry[11]!=0x20: continue
        cluster=(struct.unpack_from('<H',entry,20)[0]<<16)|struct.unpack_from('<H',entry,26)[0]
        size=struct.unpack_from('<I',entry,28)[0]
        if cluster<2 or not 0<size<=spc*bps: continue
        sector=suspected_base+(cluster-2)*spc
        mapped[sector]=(entry[:8].decode()+'.TXT',size)
    found=[]
    header='时间(hh:mm:ss)    尿量(ml)    平均流速(ml/h)  \r\n'.encode('utf8')
    source.seek((reserved+fatlen)*bps)
    fat=source.read(fatlen*bps)
    for offset in range(0,len(fat),bps):
        if not fat[offset:].startswith(header): continue
        sector=reserved+fatlen+offset//bps
        block=fat[offset:offset+spc*bps]
        end=len(header)
        while True:
            stop=block.find(b'\r\n',end)
            if stop<0: break
            line=block[end:stop]
            if not re.fullmatch(rb'\d{2}:\d{2}:\d{2}\s+[0-9.]+ml\s+[0-9.]+ml/h\s*',line): break
            end=stop+2
        name, claimed_size=mapped.get(sector,(f'REC{len(found)+1:04d}.TXT',None))
        data=block[:end]
        if claimed_size is not None and claimed_size!=len(data):
            name=f'REC{len(found)+1:04d}.TXT'
        path=recovered/name
        if path.exists() and path.read_bytes()!=data: raise RuntimeError('同名恢复记录不同，禁止覆盖')
        path.write_bytes(data)
        found.append({'file':name,'volume_sector':sector,'bytes':len(data),'directory_length':claimed_size,'content':data.decode('utf8')})
(root/'recovered-records.json').write_text(json.dumps(found,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(found,ensure_ascii=False,indent=2))
