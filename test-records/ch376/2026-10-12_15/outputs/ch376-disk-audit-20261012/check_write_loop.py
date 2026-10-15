from pathlib import Path
import sys
sys.path.insert(0,r'F:/ESP32/niaio/NIAIO-Mobile-V1-20261009/source/tests/mobile-ap-smoke')
from rv32_host import compile_elf
out=Path(__file__).resolve().parent
original=Path(r'F:/ESP32/niaio/git/third/main/driver/ch376/ch376_spi.c').read_text(encoding='utf-8-sig')
start=original.index('uint8_t CH376T_BYTE_WRITE_DATA(')
end=original.index('//关闭文件',start)
function=original[start:end]
header='''#include <stdint.h>
#include <stddef.h>
#define CMD_BYTE_WRITE 0x3c
#define CMD_WR_REQ_DATA 0x2d
#define CMD_BYTE_WR_GO 0x3d
#define USB_INT_DISK_WRITE 0x1e
#define USB_INT_SUCCESS 0x14
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
static unsigned scenario,wait_calls,requests,go_commands,writes;
void CH376_Cmd(uint8_t cmd){if(cmd==CMD_BYTE_WR_GO)go_commands++;}
void CH376_Write(uint8_t value){(void)value;writes++;}
void CH376_Cmd_End(void){}
uint8_t CH376_Read(void){requests++;return scenario==1?0:scenario==2?64:3;}
uint8_t wait(void){wait_calls++;return scenario==1?(wait_calls<=4?0x1e:0xff):(wait_calls==1?0x1e:0x14);}
'''
tests='''
int test_normal_write_completes(void){uint8_t data[3]={1,2,3};return CH376T_BYTE_WRITE_DATA(data,3)!=1||requests!=1||go_commands!=1||wait_calls!=2;}
int test_zero_request_repeats_until_external_error(void){scenario=1;uint8_t data[3]={1,2,3};uint8_t ok=CH376T_BYTE_WRITE_DATA(data,3);return ok!=0||requests!=4||go_commands!=4||wait_calls!=5||writes!=2;}
int test_oversize_request_is_clamped_and_accepted(void){scenario=2;uint8_t data[3]={1,2,3};uint8_t ok=CH376T_BYTE_WRITE_DATA(data,3);return ok!=1||requests!=1||go_commands!=1||writes!=5;}
'''
fixture=out/'write_loop_fixture.c';fixture.write_text(header+function+tests,encoding='utf-8')
runner=compile_elf([fixture],[],out/'write_loop_fixture.elf')
failed=False
for name in ['test_normal_write_completes','test_zero_request_repeats_until_external_error','test_oversize_request_is_clamped_and_accepted']:
 result=runner.call(name);print(('FAIL ' if result else 'PASS ')+name);failed|=bool(result)
print('Original write function, mocked CH376. A zero-byte request makes no progress; only the injected fifth-response error ends repeated attempts. This is a code defect model, not an observation of the attached device.')
sys.exit(int(failed))
