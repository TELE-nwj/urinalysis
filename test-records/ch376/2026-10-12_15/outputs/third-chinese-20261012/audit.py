from pathlib import Path
import re, shutil, json
root=Path(r'F:/ESP32/niaio/git/third')
out=Path(__file__).resolve().parent
token=re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\r\n]*',re.S)
files=[p for p in root.rglob('*') if p.is_file() and not any(x in p.relative_to(root).parts for x in ('build','firmware','reports','.vscode')) and (p.suffix in ('.c','.h','.ps1','.cmake') or p.name in ('CMakeLists.txt','Kconfig.projbuild'))]
for p in files:
 rel=p.relative_to(root)
 dest=out/'before'/rel
 dest.parent.mkdir(parents=True,exist_ok=True)
 if not dest.exists(): shutil.copy2(p,dest)
 text=p.read_text(encoding='utf-8-sig')
 if p.suffix in ('.c','.h'):
  for m in token.finditer(text):
   if m.group().startswith(('//','/*')) and re.search('[A-Za-z]{3}',m.group()) and not re.search('[\u3400-\u9fff]',m.group()):
    print(f'{rel}:{text[:m.start()].count(chr(10))+1}: {m.group()[:700]}')
 else:
  for n,line in enumerate(text.splitlines(),1):
   if line.lstrip().startswith('#') and re.search('[A-Za-z]{3}',line) and not re.search('[\u3400-\u9fff]',line): print(f'{rel}:{n}: {line}')
print('AUDITED_SOURCE_FILES',len(files))
(out/'source-files.json').write_text(json.dumps([str(p.relative_to(root)).replace('\\','/') for p in files],ensure_ascii=False,indent=2),encoding='utf-8')
