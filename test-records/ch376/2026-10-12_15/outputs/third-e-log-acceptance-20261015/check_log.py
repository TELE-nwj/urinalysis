"""只读检查用户E版实机日志；不访问U盘，不恢复已删除文件。"""
from pathlib import Path
import json,re,hashlib,shutil

OUT=Path(__file__).resolve().parent
source=Path('G:/新建文件夹/串口日志_20261015_192927.log')
target=OUT/source.name
shutil.copy2(source,target)
data=target.read_bytes();log=data.decode('utf-8-sig');lines=log.splitlines()
states=[{'at_ms':int(t),'from':a,'to':b} for t,a,b in re.findall(r'I \((\d+)\) monitor: 状态 (\S+) -> (\S+)',log)]
start=next(s['at_ms'] for s in states if s['from']=='待开始' and s['to']=='测量中')
pauses=[];pause=None
for s in states:
    if s['from']=='测量中' and s['to']=='暂停':pause=s['at_ms']
    elif s['from']=='暂停' and s['to']=='测量中' and pause is not None:
        pauses.append({'pause_ms':pause,'resume_ms':s['at_ms'],'duration_ms':s['at_ms']-pause});pause=None
minute=re.search(r'I \((\d+)\) S1: >>>>>[^\r\n]*第1分钟 ([^\r\n]+)',log)
minute_at=int(minute.group(1));pause_ms=sum(p['duration_ms'] for p in pauses if p['resume_ms']<=minute_at)
create_count=len(re.findall(r'命令=0x34\(创建文件\)',log))
readbacks=[{'requested':int(a),'read':int(b),'result':r} for a,b,r in re.findall(r'\[内容回读\] 请求=(\d+) 实读=(\d+) 逐字节比较=(\S+)',log)]
error_lines=[line for line in lines if re.match(r'^E \(',line)]
warnings=[line for line in lines if re.match(r'^W \(',line)]
file_states=[{'stage':stage,'file':name,'dir_lba':int(d),'start_cluster':int(c),'current_cluster':int(cur),'bytes':int(size),'offset':int(offset),'buffer_lba':int(buf),'rw_lba':int(rw)}
 for stage,name,d,c,cur,size,offset,buf,rw in re.findall(r'\[文件地址\] 阶段=(\S+) 文件=(\S+) 目录绝对LBA=(\d+) 首簇=(\d+) 当前簇=(\d+) 文件长度=(\d+) 指针=(\d+) 缓冲LBA=(\d+) 当前读写LBA=(\d+)',log)]
checks={
 'version_e':'[写盘握手修复版] 20261015-E' in log,
 'only_one_create':create_count==1,
 'one_fixed_session':re.findall(r'锁定文件=(\S+)；',log)==['/U0000001.TXT'],
 'header_readback_56':{'requested':56,'read':56,'result':'一致'} in readbacks,
 'minute_readback_47':{'requested':47,'read':47,'result':'一致'} in readbacks,
 'append_at_header_end':'芯片长度=56 表头=56 追加位置=56' in log,
 'reopened_total_103':'重新打开实际长度=103 期望=103' in log,
 'minute_write_success':'第1分钟已写入U盘: /U0000001.TXT' in log,
 'no_error_lines':not error_lines,
 'no_recreation':'文件不存在，现场创建' not in log,
 'no_skipped_minute':'分钟未存储' not in log,
 'pause_excluded_correctly':abs((minute_at-start-pause_ms)-60000)<=20,
 'completed_data_in_valid_cluster':all(s['start_cluster']==6 and s['current_cluster']==6 and s['rw_lba']==16480 for s in file_states if s['stage']=='字节写完成、关闭前'),
}
result={'source':str(source),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest().upper(),
 'checks':checks,'log_side_passed':all(checks.values()),'scope':'设备侧一次建文件、一次暂停继续及一分钟追加通过；已删TXT，电脑文件交叉核对未执行',
 'states':states,'pauses':pauses,'minute1':{'at_ms':minute_at,'row':minute.group(2),'effective_ms':minute_at-start-pause_ms},
 'create_commands':create_count,'readbacks':readbacks,'file_states':file_states,'errors':error_lines,'warnings':warnings,
 'multiple_minute_append_tested':False,'pc_file_verified':False,'code_changed':False}
(OUT/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({k:v for k,v in result.items() if k not in ('file_states',)},ensure_ascii=False,indent=2))
assert all(checks.values())
