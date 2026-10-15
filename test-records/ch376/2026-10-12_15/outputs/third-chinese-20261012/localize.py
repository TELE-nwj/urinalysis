from pathlib import Path
import re,json
root=Path(r'F:/ESP32/niaio/git/third')
out=Path(__file__).resolve().parent
files=json.loads((out/'source-files.json').read_text(encoding='utf-8'))
lex=re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\r\n]*',re.S)
log=re.compile(r'(ESP_LOG[EWIDV]\s*\(\s*[^,]+,\s*)("(?:\\.|[^"\\])*")')
changes=[]
log_words={'build':'编译于','div=':'分频系数=','constant=':'分频常数=','init: synced=':'初始化：已对时=','[SCR]':'[串口屏]','err=':'错误码=','len=':'长度=','TX=':'发送引脚=','RX=':'接收引脚=',' @%d':' 波特率=%d','获取当前文件长度CH376T_GetFileSize':'获取当前文件长度','设置SPI和SDO的中断方式CH376T_SetSdoInt':'设置 SPI 和 SDO 的中断方式'}
def comment(m):
 s=m.group()
 if not s.startswith(('//','/*')):return s
 if s=='/* service */':return '/* 服务层 */'
 if s=='/* app */':return '/* 应用层 */'
 if re.fullmatch(r'/\*\s*[A-Z][A-Z0-9_]*_H\s*\*/',s):return s[:-2].rstrip()+'：头文件保护结束 */'
 if not re.search('[\u3400-\u9fff]',s) and re.search('[A-Za-z]{3}',s):
  if s.startswith('//'):
   text=s[2:].lstrip()
   label='参数表：' if re.match(r'[0-9A-F]{2}H\s',text) else '保留的未启用代码：' if text.startswith(('#define','uint8_t','CH376_Cmd_End')) else '说明：'
   return '// '+label+text
  return '/* 说明：'+s[2:-2].strip()+' */'
 return s
for rel in files:
 p=root/rel;raw=p.read_bytes();text=raw.decode('utf-8-sig');new=text
 if p.suffix in ('.c','.h'):
  new=lex.sub(comment,new)
  def translate_log(m):
   value=m.group(2)
   for old,zh in log_words.items():value=value.replace(old,zh)
   return m.group(1)+value
  new=log.sub(translate_log,new)
 elif rel=='CMakeLists.txt':
  new=new.replace("# The following five lines of boilerplate have to be in your project's\r\n# CMakeLists in this exact order for cmake to work correctly",'# 下列基础配置须按此顺序保留，确保 CMake 正确初始化 ESP-IDF 工程。')
 elif rel=='idf-env.ps1':
  new=new.replace('# Use the already installed toolchain on this computer; installs nothing.','# 使用本机已安装的工具链，不安装任何新组件。')
  new=new.replace('ESP-IDF 5.5.5 or its Python environment is missing. Adjust idf-env.ps1 for this computer.','未找到 ESP-IDF 5.5.5 或其 Python 环境，请按本机安装位置调整 idf-env.ps1。')
 elif rel=='flash.ps1':
  for old,zh in {
   'Missing flash artifact: $required. Run build.ps1 or select the delivered firmware with -UsePrebuilt.':'缺少烧录文件：$required。请先运行 build.ps1，或使用 -UsePrebuilt 选择交付固件。',
   'Flash metadata does not describe this third ESP32-S3 build.':'烧录元数据与当前 third ESP32-S3 工程不匹配。',
   'Missing or out-of-directory image: $imagePath':'镜像不存在或超出固件目录：$imagePath',
   'Empty image: $imagePath':'镜像文件为空：$imagePath',
   'sdkconfig must target esp32s3.':'sdkconfig 的目标芯片必须为 esp32s3。',
   '# No automatic COM choice: the caller must select the actual ESP32-S3 download':'# 不自动选择串口；调用者必须明确指定实际的 ESP32-S3 下载口。',
   '# port. CH340 log port COM21 is not assumed to be a firmware download port.':'# CH340 日志口 COM21 不应直接当作固件下载口。'
  }.items():new=new.replace(old,zh)
 elif p.name=='Kconfig.projbuild':
  for old,zh in {'NIAIO mobile connection':'NIAIO 手机连接','Device Wi-Fi SSID':'设备 Wi-Fi 热点名称','Device Wi-Fi password (8..63 ASCII characters)':'设备 Wi-Fi 密码（8～63 个 ASCII 字符）','Development credential. Change before production use.':'开发测试凭据，请在正式使用前修改。'}.items():new=new.replace(old,zh)
 if new!=text:
  p.write_bytes((b'\xef\xbb\xbf' if raw.startswith(b'\xef\xbb\xbf') else b'')+new.encode('utf-8'))
  changes.append(rel)
(out/'changed-files.json').write_text(json.dumps(changes,ensure_ascii=False,indent=2),encoding='utf-8')
print('CHANGED_FILES',len(changes))
for x in changes:print(x)
