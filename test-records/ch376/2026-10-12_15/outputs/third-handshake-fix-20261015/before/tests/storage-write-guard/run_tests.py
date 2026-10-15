from pathlib import Path
import subprocess,sys,json
here=Path(__file__).resolve().parent
project=here.parents[1]
runtime={}
exec((here/'rv32_host.py').read_text().replace('5_000_000','50_000_000'),runtime)
gcc,rv32=runtime['GCC'],runtime['RV32']
out=here/'.build';out.mkdir(exist_ok=True)
libgcc=subprocess.check_output([str(gcc),'-march=rv32i_zicsr_zifencei','-mabi=ilp32','-print-libgcc-file-name'],text=True).strip()
base=[str(gcc),'-march=rv32im','-mabi=ilp32','-std=c11','-Os','-ffreestanding','-fno-builtin','-fno-stack-protector','-msmall-data-limit=0','-nostdlib','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-unused-function','-Wl,-Ttext=0x10000','-Wl,-e,0x10000','-Wl,--no-relax']
cases={
 'monitor':['test_write_suspends_only_for_transaction','test_write_error_restores_capture_and_push','test_open_and_close_error_restore_guards','test_nested_guard_restores_only_outermost','test_idle_guard_does_not_arm_capture','test_owner_commands_and_snapshot','test_capacity_expiry_and_time_sync','test_key_uses_common_state_machine','test_time_sync_does_not_change_effective_elapsed'],
 'mobile':['test_push_pause_keeps_session_and_control_acks','test_pause_wins_snapshot_send_race','run_tests'],
 'capture':['test_capture_guard_keeps_pending_intervals','test_capture_guard_preserves_paused_state','test_capture_guard_preserves_initial_idle']
}
results=[]
for suite,tests in cases.items():
 source=here/(suite+'_test.c')
 sources=[str(source)]
 if suite=='mobile':sources.append(str(project/'main/service/mobile/mobile.c'))
 elf=out/(suite+'.elf')
 includes=['-I',str(here/('mobile-stubs' if suite=='mobile' else 'monitor-stubs')),'-I',str(project/'main'),'-I',str(project/'main/service/mobile')]
 subprocess.run(base+includes+sources+[libgcc,'-o',str(elf)],check=True)
 engine=rv32(elf)
 for name in tests:
  line=engine.call(name)
  passed=line==0
  results.append({'suite':suite,'test':name,'passed':passed,'failure_line':line})
  print(('PASS ' if passed else 'FAIL ')+name+(f' line {line}' if line else ''),flush=True)
(out/'results.json').write_text(json.dumps(results,indent=2),encoding='utf8')
sys.exit(0 if all(x['passed'] for x in results) else 1)
