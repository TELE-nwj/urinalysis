from pathlib import Path
import json,struct,re,hashlib

root=Path(__file__).resolve().parent
audit=root/'disk-audit'
boot=(audit/'boot-sector.bin').read_bytes()
bps=struct.unpack_from('<H',boot,11)[0];spc=boot[13]
reserved=struct.unpack_from('<H',boot,14)[0];fatsz=struct.unpack_from('<I',boot,36)[0]
data_base=reserved+boot[16]*fatsz
partition=json.loads((audit/'partition.json').read_text(encoding='utf-8-sig'))['Offset']//bps
fat1=(audit/'fat-first.bin').read_bytes();fat2=(audit/'fat-second.bin').read_bytes()
diff=[i//bps for i in range(0,len(fat1),bps) if fat1[i:i+bps]!=fat2[i:i+bps]]
entries=[]
directory=(audit/'cluster-2.bin').read_bytes()
for at in range(0,len(directory),32):
    row=directory[at:at+32]
    if row[0]==0:break
    if row[0]==0xe5 or row[11]==0x0f:continue
    if not re.fullmatch(rb'U[0-9]{7}TXT',row[:11]):continue
    name=row[:8].decode()+'.TXT'
    cluster=(struct.unpack_from('<H',row,20)[0]<<16)|struct.unpack_from('<H',row,26)[0]
    size=struct.unpack_from('<I',row,28)[0]
    fragment=audit/f'cluster-{cluster}.bin'
    disk_bytes=fragment.read_bytes()[:size] if fragment.exists() else None
    ordinary=(root/name).read_bytes()
    entries.append({'file':name,'root_offset':at,'cluster':cluster,'bytes':size,'absolute_lba':partition+data_base+(cluster-2)*spc,
        'physical_matches_file':disk_bytes==ordinary,'sha256':hashlib.sha256(ordinary).hexdigest(),'text':ordinary.decode('utf8')})
result={'partition_lba':partition,'correct_data_lba':partition+data_base,'root_lba':partition+data_base,'fat_equal':not diff,'fat_mismatch_sectors':len(diff),'fat_mismatch_indices':diff,'files':entries}
log=(root/'串口日志_20261014_112326.log').read_text(encoding='utf-8-sig')
states=[(int(t),a,b) for t,a,b in re.findall(r'I \((\d+)\) monitor: 状态 (\S+) -> (\S+)',log)]
start=next((t for t,a,b in states if a=='待开始' and b=='测量中'),None)
pause=None;intervals=[]
for t,a,b in states:
    if a=='测量中' and b=='暂停':pause=t
    if a=='暂停' and b=='测量中' and pause is not None:
        intervals.append({'start_ms':pause,'end_ms':t,'duration_ms':t-pause});pause=None
minute=re.search(r'I \((\d+)\) S1: >>>>>.*第1分钟',log)
minute_at=int(minute.group(1)) if minute else None
result['pause_resume']={'states':states,'resumed_pauses':intervals,'pause_ms':sum(i['duration_ms'] for i in intervals),'start_ms':start,'minute1_ms':minute_at,'effective_to_minute1_ms':minute_at-start-sum(i['duration_ms'] for i in intervals) if start is not None and minute_at is not None else None}
(root/'disk-review.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(result,ensure_ascii=False,indent=2))
