"""比较基线/日志副本真实写函数及等待函数的模拟指令轨迹；不访问串口或U盘。"""
from pathlib import Path
import json
import subprocess
import sys

here = Path(__file__).resolve().parent
project = here.parents[1]
baseline = project.parents[1] / 'outputs/third-logtest-20261012/baseline'
runtime = {}
exec((project / 'tests/storage-write-guard/rv32_host.py').read_text().replace('5_000_000', '60_000_000'), runtime)
gcc, RV32 = runtime['GCC'], runtime['RV32']
out = here / '.build'
out.mkdir(exist_ok=True)
flags = [str(gcc), '-march=rv32im', '-mabi=ilp32', '-std=c11', '-Os', '-ffreestanding', '-fno-builtin', '-msmall-data-limit=0', '-nostdlib', '-Wl,-Ttext=0x10000', '-Wl,-e,0x10000', '-Wl,--no-relax']
libgcc = subprocess.check_output([str(gcc), '-march=rv32i_zicsr_zifencei', '-mabi=ilp32', '-print-libgcc-file-name'], text=True).strip()

prefix = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define CMD_BYTE_WRITE 0x3c
#define CMD_WR_REQ_DATA 0x2d
#define CMD_BYTE_WR_GO 0x3d
#define USB_INT_DISK_WRITE 0x1e
#define USB_INT_SUCCESS 0x14
#define CH376_INT_GPIO 5
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define pdMS_TO_TICKS(x) 0
static uint8_t s_trace_cmd;
static uint32_t s_trace_seq;
static bool s_trace_selected;
static int64_t s_trace_started;
static unsigned scenario, hash, calls, polls, delay_calls, yields;
static unsigned target_polls;
static void event(unsigned v) { hash = (hash ^ v) * 16777619u; }
void CH376_Cmd(uint8_t cmd) { event(0x100u | cmd); }
void CH376_Write(uint8_t value) { event(0x200u | value); }
void CH376_Cmd_End(void) { event(0x300u); }
uint8_t CH376_Read(void) {
    unsigned n = scenario == 1 ? 0 : scenario == 2 ? 64 : scenario == 3 ? 2 : 3;
    event(0x400u | n); return n;
}
uint8_t mock_wait(void) {
    calls++;
    unsigned s = scenario == 1 ? (calls <= 4 ? 0x1e : 0xff) :
                 scenario == 3 ? (calls <= 2 ? 0x1e : 0x14) :
                 scenario == 4 ? 0x20 : (calls == 1 ? 0x1e : 0x14);
    event(0x500u | s); return s;
}
int gpio_get_level(int pin) { (void)pin; return delay_calls < target_polls; }
void esp_rom_delay_us(unsigned us) { (void)us; delay_calls++; }
void vTaskDelay(unsigned ticks) { (void)ticks; yields++; }
int64_t esp_timer_get_time(void) { return delay_calls * 10; }
uint8_t CH376_GetStatus(void) { calls++; event(0x614); return 0x14; }
void CH376_DiagFile(const char *stage, const char *name) { (void)stage; (void)name; }
'''
suffix = r'''
unsigned test_normal(void) { uint8_t b[3] = {1,2,3}; event(CH376T_BYTE_WRITE_DATA(b,3)); return hash; }
unsigned test_zero_progress(void) { scenario=1; return test_normal(); }
unsigned test_oversize(void) { scenario=2; return test_normal(); }
unsigned test_multiblock(void) { scenario=3; return test_normal(); }
unsigned test_request_error(void) { scenario=4; return test_normal(); }
unsigned test_wait_immediate(void) { return wait() != 0x14 || delay_calls || calls != 1; }
unsigned test_wait_delayed(void) { target_polls=600; return wait() != 0x14 || delay_calls != 600 || yields != 2 || calls != 1; }
unsigned test_wait_timeout(void) { target_polls=600000; return wait() != 0xff || delay_calls != 500001 || calls || yields != (500001u/256u); }
'''

engines = {}
for label, root in [('baseline', baseline), ('logtest', project)]:
    source = (root / 'main/driver/ch376/ch376_spi.c').read_text(encoding='utf-8-sig')
    write = source[source.index('uint8_t CH376T_BYTE_WRITE_DATA('):source.index('//关闭文件', source.index('uint8_t CH376T_BYTE_WRITE_DATA('))]
    wait = source[source.index('uint8_t wait(void)'):source.index('uint8_t CH376_GetStatus(void)')]
    fixture = out / (label + '.c')
    fixture.write_text(prefix + '#define wait mock_wait\n' + write + '\n#undef wait\n' + wait + suffix, encoding='utf-8')
    elf = out / (label + '.elf')
    subprocess.run(flags + [str(fixture), libgcc, '-o', str(elf)], check=True)
    engines[label] = RV32(elf)

results = []
for name in ['test_normal', 'test_zero_progress', 'test_oversize', 'test_multiblock', 'test_request_error', 'test_wait_immediate', 'test_wait_delayed', 'test_wait_timeout']:
    a, b = (engines[label].call(name) for label in ['baseline', 'logtest'])
    passed = a == b and (not name.startswith('test_wait_') or a == 0)
    results.append({'case': name, 'baseline': a, 'logtest': b, 'passed': passed})
    print(('PASS ' if passed else 'FAIL ') + name, flush=True)
(out / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
sys.exit(0 if all(r['passed'] for r in results) else 1)
