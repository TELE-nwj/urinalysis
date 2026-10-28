"""Compile the real driver with host-only IDF stubs and execute RV32IM C.

No SDK/CRT installation is needed; uses the parent-owned RV32 test executor.
--red reproduces the first expected failure with an unimplemented API fixture.
"""
import argparse
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from rv32_host import compile_elf

p = argparse.ArgumentParser()
p.add_argument('--red', action='store_true')
args = p.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[2]
driver = root / 'second sol/main/driver/mobile_link'
out = here / '.build'
out.mkdir(exist_ok=True)
sources = [here / 'host_test.c', here / 'red_driver.c' if args.red else driver / 'mobile_link.c']
elf = out / ('red.elf' if args.red else 'mobile_link_tests.elf')
machine = compile_elf(sources, [here / 'stubs', driver, root / 'second sol/main'], elf)
result = machine.call('run_tests')
if result:
    assertion = (here / 'host_test.c').read_text().splitlines()[result - 1].strip()
    print(f'FAIL host_test.c:{result}: {assertion}')
    sys.exit(1)
print('PASS mobile_link: all lifecycle, frame, token, queue and callback assertions')
