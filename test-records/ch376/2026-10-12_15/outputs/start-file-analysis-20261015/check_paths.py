"""Execute real monitor/S1 and storage C with controlled storage/chip doubles.

Evidence for control flow only; no serial port, USB access or firmware changes.
"""
from pathlib import Path
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
PROJECT = ROOT / 'git/third_logtest_20261012'
GUARD = PROJECT / 'tests/storage-write-guard'
FILES = [ROOT / 'git' / version / 'main' / name
         for version in ['third', 'third_logtest_20261012']
         for name in ['app/monitor/monitor.c', 'app/S1/S1.c',
                      'service/storage/storage.c', 'driver/ch376/ch376_spi.c']]

def hashes():
    return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in FILES}

before = hashes()
runtime = {'__file__': str(GUARD / 'rv32_host.py')}
exec((GUARD / 'rv32_host.py').read_text(encoding='utf8').replace('5_000_000', '50_000_000'), runtime)
gcc, RV32 = runtime['GCC'], runtime['RV32']
libgcc = subprocess.check_output([str(gcc), '-march=rv32i_zicsr_zifencei', '-mabi=ilp32', '-print-libgcc-file-name'], text=True).strip()
base = [str(gcc), '-march=rv32im', '-mabi=ilp32', '-std=c11', '-Os',
        '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
        '-msmall-data-limit=0', '-nostdlib', '-Wl,-Ttext=0x10000',
        '-Wl,-e,0x10000', '-Wl,--no-relax', '-I', str(GUARD / 'monitor-stubs'),
        '-I', str(PROJECT / 'main')]

monitor = (GUARD / 'monitor_test.c').read_text(encoding='utf8')
monitor = monitor.replace('static uint64_t now_ms;', 'static uint64_t now_ms;\nstatic unsigned create_calls, ensure_calls;\nstatic int create_error;')
monitor = monitor.replace('now_ms+=storage_delay_ms;memcpy(b,"U0000001",9);return ERR_OK;',
                          'now_ms+=storage_delay_ms;create_calls++;if(create_error)return ERR_FAIL;memcpy(b,"U0000001",9);return ERR_OK;')
monitor = monitor.replace('storage_ensure_session(const char*s){(void)s;return ERR_OK;}',
                          'storage_ensure_session(const char*s){(void)s;ensure_calls++;return ERR_OK;}')
monitor += r'''
int test_healthy_resume_reuses_session_reset_start_creates(void){
 CHECK(app_monitor_start()==ERR_OK);key_cb(INPUT_KEY1);
 CHECK(create_calls==1&&app_monitor_state()==APP_MEASURING);
 CHECK(submit(APP_COMMAND_START,1,5000)==ERR_OK);monitor_step();CHECK(create_calls==1);
 key_cb(INPUT_KEY1);CHECK(app_monitor_state()==APP_PAUSED);
 now_ms=10000;key_cb(INPUT_KEY1);monitor_step();
 CHECK(app_monitor_state()==APP_MEASURING&&create_calls==1&&ensure_calls==0);
 key_cb(INPUT_KEY2);CHECK(app_monitor_state()==APP_IDLE);
 key_cb(INPUT_KEY1);CHECK(create_calls==2);return 0;
}
int test_failed_creation_retry_waits_for_resume(void){
 CHECK(app_monitor_start()==ERR_OK);create_error=1;key_cb(INPUT_KEY1);
 CHECK(create_calls==1&&S1_session_file()[0]==0&&!s_storage_ok);
 key_cb(INPUT_KEY1);CHECK(app_monitor_state()==APP_PAUSED);
 now_ms=10000;monitor_step();CHECK(create_calls==1);
 create_error=0;key_cb(INPUT_KEY1);CHECK(create_calls==1);
 monitor_step();CHECK(create_calls==2&&ensure_calls==0&&s_storage_ok);return 0;
}
int test_known_session_error_retry_uses_ensure_after_resume(void){
 CHECK(app_monitor_start()==ERR_OK);key_cb(INPUT_KEY1);write_error=ERR_FAIL;
 s1_record_write(1);CHECK(!s_storage_ok&&S1_session_file()[0]);
 key_cb(INPUT_KEY1);now_ms=10000;monitor_step();CHECK(ensure_calls==0);
 key_cb(INPUT_KEY1);monitor_step();CHECK(create_calls==1&&ensure_calls==1);return 0;
}
'''

storage = (PROJECT / 'tests/address-diagnostics/storage_fixture.c').read_text(encoding='utf8')
storage = storage.replace('static unsigned verify_calls,write_calls;', 'static unsigned verify_calls,write_calls,create_calls;')
storage = storage.replace('CH376T_FILE_CREATE_NAME(const char*s){(void)s;return true;}',
                          'CH376T_FILE_CREATE_NAME(const char*s){(void)s;create_calls++;return true;}')
storage = storage.replace('#include "../../main/service/storage/storage.c"', '#include "service/storage/storage.c"')
storage += r'''
int test_created_but_failed_validation_leaves_session_empty(void){
 char stem[9]={0};CHECK(storage_init()==ERR_OK);open_code=0x42;verify_ok=false;
 CHECK(storage_create_session(stem,sizeof(stem))==ERR_FAIL);
 CHECK(create_calls==1&&write_calls==1&&verify_calls==1&&stem[0]==0&&s_next_session==1);return 0;
}
int test_append_open_missing_automatically_recreates(void){
 CHECK(storage_init()==ERR_OK);open_code=0x42;
 CHECK(storage_open_session("U0000001")==ERR_OK);
 CHECK(create_calls==1&&write_calls==1&&s_file_open);return 0;
}
int test_ensure_open_missing_automatically_recreates(void){
 CHECK(storage_init()==ERR_OK);open_code=0x42;
 CHECK(storage_ensure_session("U0000001")==ERR_OK);
 CHECK(create_calls==1&&write_calls==1&&verify_calls==1);return 0;
}
'''
cases = {
    'monitor': (monitor, ['test_healthy_resume_reuses_session_reset_start_creates',
                          'test_failed_creation_retry_waits_for_resume',
                          'test_known_session_error_retry_uses_ensure_after_resume']),
    'storage': (storage, ['test_created_but_failed_validation_leaves_session_empty',
                          'test_append_open_missing_automatically_recreates',
                          'test_ensure_open_missing_automatically_recreates'])}
results = []
for suite, (source, names) in cases.items():
    cfile, elf = OUT / (suite + '.c'), OUT / (suite + '.elf')
    cfile.write_text(source, encoding='utf8')
    subprocess.run(base + [str(cfile), libgcc, '-o', str(elf)], check=True)
    engine = RV32(elf)
    for name in names:
        line = engine.call(name)
        results.append({'test': name, 'observed_expected_current_behavior': line == 0, 'failure_line': line})
        print(('CONFIRMED ' if line == 0 else 'FAIL ') + name + (f' line {line}' if line else ''), flush=True)
after = hashes()
report = {'purpose': 'Reproduce current control flow, including unsafe recreation; not acceptance of a fix.',
          'results': results, 'inspected_sources_unchanged': before == after, 'source_sha256': after}
(OUT / 'results.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
assert before == after
assert all(r['observed_expected_current_behavior'] for r in results)
