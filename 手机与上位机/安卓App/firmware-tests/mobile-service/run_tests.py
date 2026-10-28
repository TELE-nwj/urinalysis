"""Compile production C as RV32IM and execute with the existing CPU runner.

The formatter double implements the tiny printf format subset used by service;
the SDK build separately checks the production newlib formatting calls.
"""
from pathlib import Path
import subprocess, sys
here = Path(__file__).resolve().parent
root = here.parents[1]
sys.path.insert(0, str(root / 'tests/mobile-ap-smoke'))
import rv32_host
# The service suite includes printf/soft-float and 33 cache eviction requests.
# Reuse the audited integer CPU runner with a larger instruction budget only.
runner = {}
exec((root / 'tests/mobile-ap-smoke/rv32_host.py').read_text().replace('5_000_000', '50_000_000'), runner)
GCC, RV32 = runner['GCC'], runner['RV32']
out = here / '.build'
out.mkdir(exist_ok=True)
sources = [here / 'host_test.c', here / 'red_service.c' if '--red' in sys.argv else root / 'second sol/main/service/mobile/mobile.c']
if '--race-red' in sys.argv:
    baseline = out / 'race_red.c'
    baseline.write_text(sources[1].read_text().replace('if (refresh) {', 'if (refresh && 0) {'))
    sources[1] = baseline
elf = out / 'tests.elf'
command = [str(GCC), '-march=rv32im', '-mabi=ilp32', '-std=c11', '-Os', '-ffreestanding', '-fno-builtin', '-fno-stack-protector', '-msmall-data-limit=0', '-nostdlib', '-Wall', '-Wextra', '-Werror', '-Wl,-Ttext=0x10000', '-Wl,-e,0x10000', '-Wl,--no-relax', '-I', str(here / 'stubs'), '-I', str(root / 'second sol/main'), '-I', str(root / 'second sol/main/service/mobile')]
libgcc = subprocess.check_output([str(GCC), '-march=rv32i_zicsr_zifencei', '-mabi=ilp32', '-print-libgcc-file-name'], text=True).strip()
subprocess.run(command + list(map(str,sources)) + [libgcc, '-o', str(elf)], check=True)
line = RV32(elf).call('run_tests')
if line:
    print(f'FAIL host_test.c:{line}: { (here / "host_test.c").read_text().splitlines()[line - 1] }')
    sys.exit(1)
print('PASS mobile service: strict JSON, admission, dedup, sessions, completion, snapshots')
