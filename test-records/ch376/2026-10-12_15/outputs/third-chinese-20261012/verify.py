from pathlib import Path
import re,json
root=Path(r'F:/ESP32/niaio/git/third');out=Path(__file__).resolve().parent
files=json.loads((out/'source-files.json').read_text(encoding='utf-8'))
lex=re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\r\n]*',re.S)
log=re.compile(r'(ESP_LOG[EWIDV]\s*\(\s*[^,]+,\s*)("(?:\\.|[^"\\])*")')
fmt=re.compile(r'%(?:\d+\$)?[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|[hljztL])?[diouxXfFeEgGaAcspn%]')
tokens=re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_]\w*|\d+(?:\.\d+)?|[^\s]',re.S)
def no_comments(s):return lex.sub(lambda m:' ' if m.group().startswith(('//','/*')) else m.group(),s)
def code(s):return tokens.findall(log.sub(lambda m:m.group(1)+'"<日志文案>"',no_comments(s)))
failures=[];updated_logs=0;english_comments=[]
for rel in files:
 p=root/rel;before=(out/'before'/rel).read_text(encoding='utf-8-sig');after=p.read_text(encoding='utf-8-sig')
 if p.suffix in ('.c','.h'):
  if code(before)!=code(after):failures.append(rel+':非注释或日志的C词法单元发生变化')
  old=log.findall(no_comments(before));new=log.findall(no_comments(after))
  if len(old)!=len(new):failures.append(rel+':日志调用数量改变')
  for (_,a),(_,b) in zip(old,new):
   if fmt.findall(a)!=fmt.findall(b):failures.append(rel+':日志格式占位符改变')
   updated_logs+=a!=b
  for m in lex.finditer(after):
   if m.group().startswith(('//','/*')) and re.search('[A-Za-z]{3}',m.group()) and not re.search('[\u3400-\u9fff]',m.group()):english_comments.append(rel)
 elif p.name=='Kconfig.projbuild':
  keys=lambda s:re.findall(r'^\s*(config|default)\s+(.+)$',s,re.M)
  if keys(before)!=keys(after):failures.append(rel+':配置键或默认值改变')
 elif p.name=='CMakeLists.txt':
  clean=lambda s:[x.strip() for x in s.splitlines() if x.strip() and not x.lstrip().startswith('#')]
  if clean(before)!=clean(after):failures.append(rel+':构建配置改变')
report={'checked_source_files':len(files),'changed_files':json.loads((out/'changed-files.json').read_text(encoding='utf-8')),'changed_log_messages':updated_logs,'remaining_english_only_comments':english_comments,'functional_token_failures':failures,'c_tokens_preserved_except_log_format_literals':not failures,'log_format_placeholders_preserved':not failures}
(out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k!='changed_files'},ensure_ascii=False))
if failures or english_comments:raise SystemExit(1)
