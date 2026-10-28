from pathlib import Path
import subprocess,json,sys
here=Path(__file__).resolve().parent
project=here.parents[1]
runtime={};exec((project/'tests/storage-write-guard/rv32_host.py').read_text().replace('5_000_000','50_000_000'),runtime)
gcc,RV32=runtime['GCC'],runtime['RV32']
out=here/'.build';out.mkdir(exist_ok=True)
libgcc=subprocess.check_output([str(gcc),'-march=rv32i_zicsr_zifencei','-mabi=ilp32','-print-libgcc-file-name'],text=True).strip()
base=[str(gcc),'-march=rv32im','-mabi=ilp32','-std=c11','-Os','-ffreestanding','-fno-builtin','-msmall-data-limit=0','-nostdlib','-Wl,-Ttext=0x10000','-Wl,-e,0x10000','-Wl,--no-relax','-I',str(project/'tests/storage-write-guard/monitor-stubs'),'-I',str(project/'main')]
names=['test_minute_close_requires_readback','test_readback_failure_reports_error','test_close_failure_skips_verification','test_compat_failure_stops_initialization','test_mount_failure_stops_initialization','test_no_write_no_readback','test_multiple_appends_last_range','test_new_header_requires_readback']
results=[];old_failures=0
for version in ['baseline','current']:
    elf=out/(version+'-storage.elf')
    flags=['-DBASELINE'] if version=='baseline' else []
    subprocess.run(base+flags+[str(here/'storage_fixture.c'),libgcc,'-o',str(elf)],check=True)
    engine=RV32(elf)
    for name in names:
        line=engine.call(name)
        if version=='baseline':old_failures+=bool(line)
        else:results.append({'case':name,'passed':line==0,'failure_line':line})
        print(f'{version}: '+('PASS ' if line==0 else 'FAIL ')+name,flush=True)
(out/'storage-results.json').write_text(json.dumps({'baseline_failures':old_failures,'current':results},indent=2),encoding='utf8')
sys.exit(0 if old_failures>=4 and all(x['passed'] for x in results) else 1)
