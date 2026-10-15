"""真实 monitor/S1/storage 联合回归：只有 CH376 与 SDK 使用桩，不访问设备。"""
from pathlib import Path
import argparse
import json
import re
import subprocess
import sys

here = Path(__file__).resolve().parent
project = here.parents[1]
guard = project / 'tests/storage-write-guard'
parser = argparse.ArgumentParser()
parser.add_argument('--label', default='current')
args = parser.parse_args()
out = here / '.build'
out.mkdir(exist_ok=True)
runtime = {}
exec((guard / 'rv32_host.py').read_text().replace('5_000_000', '50_000_000'), runtime)
gcc, RV32 = runtime['GCC'], runtime['RV32']
libgcc = subprocess.check_output([str(gcc), '-march=rv32i_zicsr_zifencei', '-mabi=ilp32', '-print-libgcc-file-name'], text=True).strip()

# 复用现有 SDK/采集桩，但删除存储桩，接入真实存储实现。
source = (guard / 'monitor_test.c').read_text(encoding='utf8')
for name in ['storage_create_session', 'storage_ensure_session', 'storage_open_session',
             'storage_write_open', 'storage_close_session', 'storage_get_status', 'snprintf']:
    source, count = re.subn(r'^.*\b' + name + r'\([^\n]*\)\{[^\n]*\}\s*$', '', source, flags=re.M)
    if count != 1:
        raise RuntimeError(f'预期替换一处桩 {name}，实际 {count}')
source += '\n#include "' + (here / 'session_model.c').as_posix() + '"\n'
generated = out / 'integration.c'
generated.write_text(source, encoding='utf8')
elf = out / 'integration.elf'
command = [str(gcc), '-march=rv32im', '-mabi=ilp32', '-std=c11', '-Os', '-ffreestanding',
           '-fno-builtin', '-fno-stack-protector', '-msmall-data-limit=0', '-nostdlib',
           '-Wall', '-Wextra', '-Werror', '-Wno-unused-variable', '-Wno-unused-function',
           '-Wl,-Ttext=0x10000', '-Wl,-e,0x10000', '-Wl,--no-relax',
           '-I', str(guard / 'monitor-stubs'), '-I', str(project / 'main'),
           str(generated), libgcc, '-o', str(elf)]
subprocess.run(command, check=True)
engine = RV32(elf)
names = re.findall(r'^int (test_session_\w+)\(void\)', (here / 'session_model.c').read_text(encoding='utf8'), re.M)
results = []
for name in names:
    line = engine.call(name)
    results.append({'test': name, 'passed': line == 0, 'failure_line': line})
    print(('PASS ' if line == 0 else 'FAIL ') + name + (f' line {line}' if line else ''), flush=True)
(out / (args.label + '-results.json')).write_text(json.dumps(results, indent=2), encoding='utf8')
sys.exit(0 if all(r['passed'] for r in results) else 1)
