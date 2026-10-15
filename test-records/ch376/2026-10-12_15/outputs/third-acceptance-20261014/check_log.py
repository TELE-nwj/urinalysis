"""只读验收串口日志；不访问串口、固件或U盘。真实文件对照另做。"""
from pathlib import Path
import sys,json,re,hashlib

source=Path(sys.argv[1])
raw=source.read_bytes()
encoding=None
for trial in ['utf-8-sig','utf-16','gb18030']:
    try:
        log=raw.decode(trial);encoding=trial;break
    except UnicodeError:pass
if encoding is None: raise RuntimeError('无法可靠解码日志')

parts=re.split(r'(?=ESP-ROM:esp32s3)',log)
sessions=[]
for part in parts:
    if not part.strip():continue
    expected=[int(x) for x in re.findall(r'\[几何期望\].*?数据区绝对LBA=(\d+)',part)]
    compatibility=[]
    for m in re.finditer(r'\[兼容结论\].*?处理后0x4C=(\d+) 数据区=(\d+) 复读=(\d+) 差值=(-?\d+) 根簇=(\d+) 每簇=(\d+) 结果=([^\r\n]+)',part):
        base,data,repeat,diff,root,spc,result=m.groups()
        compatibility.append({'internal_base':int(base),'data_lba':int(data),'repeat':int(repeat),'delta':int(diff),'root_cluster':int(root),'sectors_per_cluster':int(spc),'result':result,'passed':result=='通过' and int(diff)==0 and data==repeat and (not expected or int(data)==expected[-1])})
    records=[]
    for m in re.finditer(r'\[分钟验收\] 文件=(\S+) 期望总长度=(\d+) 追加起点=(\d+) 新增内容=(.*?) 回读结果=(通过|失败)',part,re.S):
        name,size,offset,text,result=m.groups()
        records.append({'file':name,'expected_size':int(size),'append_offset':int(offset),'text':text,'passed':result=='通过'})
    sessions.append({'version_B':'[写盘兼容验证版] 20261014-B' in part,'expected_data_lba':expected,
        'compatibility':compatibility,'new_files':re.findall(r'新测量文件: (\S+)',part),
        'minute_readback':records,'readback_matches':len(re.findall(r'\[内容回读\].*?逐字节比较=一致',part)),
        'readback_failures':len(re.findall(r'\[持久化验证\].*?结论=失败',part)),
        'write_blocks':re.findall(r'[^\r\n]*\[(?:写盘拦截|写盘保护)\][^\r\n]*(?:禁止|拦截)[^\r\n]*',part),
        'panic':'Guru Meditation' in part or 'panic_abort' in part})
report={'source':str(source),'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest(),'encoding':encoding,
        'sessions':sessions,'usb_files_verified':False,'replug_verified':False}
output=Path(__file__).resolve().parent/'log-review.json'
output.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(report,ensure_ascii=False,indent=2))
