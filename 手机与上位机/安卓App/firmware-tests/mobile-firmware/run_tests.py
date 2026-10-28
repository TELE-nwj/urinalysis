import argparse, pathlib, sys, subprocess
HERE=pathlib.Path(__file__).resolve().parent
ROOT=HERE.parents[1]
sys.path.insert(0,str(ROOT/'tests/mobile-ap-smoke'))
from rv32_host import GCC,RV32
p=argparse.ArgumentParser();p.add_argument('--red',action='store_true');p.add_argument('--storage',action='store_true');p.add_argument('--app-red',action='store_true');a=p.parse_args()
out=HERE/'build-host';out.mkdir(exist_ok=True)
if a.red:
    module='service/storage/storage.c' if a.storage else 'app/monitor/monitor.c'
    fixture='baseline_storage.c' if a.storage else 'baseline_monitor.c'
    original=subprocess.check_output(['git','show','6153f29:second sol/main/'+module],cwd=ROOT)
    header=module.removesuffix('.c')+'.h'
    original=original.replace(('#include "'+pathlib.Path(header).name+'"').encode(),('#include "'+header+'"').encode(),1)
    (out/fixture).write_bytes(original)
cmd=[str(GCC),'-march=rv32im','-mabi=ilp32','-Os','-ffreestanding','-fno-builtin','-fno-stack-protector','-msmall-data-limit=0','-nostdlib','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-unused-function','-Wl,-Ttext=0x10000','-Wl,-e,0x10000','-Wl,--no-relax','-I',str(HERE/'stubs'),'-I',str(ROOT/'second sol/main')]
if a.red:cmd+=['-DRED_STORAGE' if a.storage else '-DRED_BASELINE']
if a.app_red:cmd+=['-DRED_APP']
libgcc=GCC.parent.parent/'lib/gcc/riscv32-esp-elf/14.2.0/rv32i_zicsr_zifencei/ilp32/libgcc.a'
source='storage_test.c' if a.storage else 'monitor_test.c'
elf=out/('storage.elf' if a.storage else 'monitor.elf')
cmd+=[str(HERE/source),str(libgcc),'-o',str(elf)]
subprocess.run(cmd,check=True)
runner=RV32(elf)
tests=['test_external_reset_is_queued'] if a.red else ['test_external_reset_is_queued','test_owner_commands_and_snapshot','test_capacity_expiry_and_time_sync','test_key_uses_common_state_machine','test_snapshot_identity_and_elapsed_over_24h']
if a.storage:tests=['test_storage_cache_observes_errors_and_absence','test_storage_io_failure_not_absent','test_mount_warning_remains_error_until_verified','test_reinserted_media_io_error_does_not_reuse_absence','test_not_ready_without_current_disconnect_is_not_absent']
elif a.app_red:tests=['test_mobile_adapter_uses_monitor_queue_and_permanent_callback']
elif not a.red:tests+=['test_mobile_adapter_uses_monitor_queue_and_permanent_callback','test_failed_monitor_start_is_not_ready','test_started_storage_transaction_finishes_and_next_request_expires','test_time_sync_does_not_change_effective_elapsed']
failed=False
for t in tests:
    r=runner.call(t);print(('FAIL' if r else 'PASS')+' '+t+(f' assertion line {r}' if r else ''));failed|=bool(r)
sys.exit(int(failed))
