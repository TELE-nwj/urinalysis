from pathlib import Path
import re,json
base=Path(r'F:/ESP32/niaio/git')
lex=re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\r\n]*',re.S)
def clean(s):
 s=lex.sub(lambda m:' ' if m.group().startswith(('//','/*')) else m.group(),s)
 s=re.sub(r'ESP_LOG[IEWDV]\s*\(.*?\);',' ',s,flags=re.S)
 return s
def function(s,name):
 start=re.search(r'\b(?:void|uint8_t|uint32_t)\s+'+name+r'\s*\(',s).start()
 begin=s.index('{',start);depth=1;i=begin+1
 while depth:
  if s[i]=='{':depth+=1
  if s[i]=='}':depth-=1
  i+=1
 return re.findall(r'"(?:\\.|[^"\\])*"|[A-Za-z_]\w*|\d+|[^\s]',s[start:i])
old=clean((base/'second/main/driver/ch376/ch376_spi.c').read_text(encoding='utf-8-sig'))
new=clean((base/'third/main/driver/ch376/ch376_spi.c').read_text(encoding='utf-8-sig'))
names=['CH376_PORT_INIT','ch376_spi_OutByte','ch376_spi_InByte','CH376_Cmd','CH376_Read','CH376_Write','wait','CH376T_GetFileSize','CH376T_SetUsbMode','CH376T_CMD_DISK_CONNECT','CH376T_DISK_MOUNT','CH376T_FILE_CREATE_NAME','CH376T_BYTE_WRITE_DATA','CH376T_FILE_CLOSE','CH376T_BYTE_LOCATE']
result={name:function(old,name)==function(new,name) for name in names}
out=Path(__file__).resolve().parent
(out/'driver-comparison.json').write_text(json.dumps(result,indent=2),encoding='utf8')
print(json.dumps(result,indent=2))
