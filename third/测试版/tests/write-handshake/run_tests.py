"""复现真实D握手故障：真实diag/字节写 + 不稳定内部变量窗口模型。"""
from pathlib import Path
import json,re,subprocess,sys,argparse
here=Path(__file__).resolve().parent
project=here.parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--label',default='current');parser.add_argument('--baseline-d',action='store_true');args=parser.parse_args()
source_root=project.parents[1]/'outputs/third-handshake-fix-20261015/before' if args.baseline_d else project
runtime={};exec((project/'tests/storage-write-guard/rv32_host.py').read_text().replace('5_000_000','50_000_000'),runtime)
gcc,RV32=runtime['GCC'],runtime['RV32']
out=here/'.build';out.mkdir(exist_ok=True)
fixture=(project/'tests/address-diagnostics/fixture.c').read_text(encoding='utf8')
fixture=fixture.replace('#include "../../main/driver/ch376/ch376_diag.c"','#include "'+(source_root/'main/driver/ch376/ch376_diag.c').as_posix()+'"')
driver=(source_root/'main/driver/ch376/ch376_spi.c').read_text(encoding='utf8')
begin=driver.index('uint8_t CH376T_BYTE_WRITE_DATA(')
write=driver[begin:driver.index('//关闭文件',begin)]
cases=(here/'cases.c').read_text(encoding='utf8')
generated=out/'integration.c';generated.write_text(fixture+'\n'+write+'\n'+cases,encoding='utf8')
libgcc=subprocess.check_output([str(gcc),'-march=rv32i_zicsr_zifencei','-mabi=ilp32','-print-libgcc-file-name'],text=True).strip()
elf=out/'integration.elf'
command=[str(gcc),'-march=rv32im','-mabi=ilp32','-std=c11','-Os','-ffreestanding','-fno-builtin','-msmall-data-limit=0','-nostdlib','-Wl,-Ttext=0x10000','-Wl,-e,0x10000','-Wl,--no-relax','-DCMD_BYTE_WRITE=0x3c','-DCMD_WR_REQ_DATA=0x2d','-DCMD_BYTE_WR_GO=0x3d','-DUSB_INT_DISK_WRITE=0x1e','-I',str(project/'main'),'-I',str(project/'tests/storage-write-guard/monitor-stubs'),str(generated),libgcc,'-o',str(elf)]
subprocess.run(command,check=True);engine=RV32(elf)
results=[]
for name in re.findall(r'^int (test_handshake_\w+)\(void\)',cases,re.M):
    line=engine.call(name);results.append({'test':name,'passed':line==0,'failure_line':line})
    print(('PASS ' if line==0 else 'FAIL ')+name+(f' line {line}' if line else ''),flush=True)
(out/(args.label+'-results.json')).write_text(json.dumps(results,indent=2),encoding='utf8')
sys.exit(0 if all(r['passed'] for r in results) else 1)
