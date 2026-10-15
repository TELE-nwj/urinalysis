#include <stdint.h>
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
uint8_t CH376T_BYTE_WRITE_DATA(const uint8_t *data, uint16_t len)
{
    uint8_t status;
    uint16_t remaining = len;
    uint16_t idx = 0;

    if (data == NULL || len == 0) return 0;

    CH376_Cmd(CMD_BYTE_WRITE); /* 0x3c */
    CH376_Write(len & 0xFF);   /* 长度低字节 */
    CH376_Write((len >> 8) & 0xFF);
    CH376_Cmd_End();

    status = wait();
    if (status != USB_INT_DISK_WRITE) { /* 0x1E */
        ESP_LOGE(TAG, "请求写入失败: 0x%02X", status);
        return 0;
    }

    do {
        CH376_Cmd(CMD_WR_REQ_DATA); /* 0x2d */
        uint8_t n = CH376_Read();
        if (n > remaining) n = remaining;
        for (uint8_t i = 0; i < n; i++) {
            CH376_Write(data[idx++]);
        }
        remaining -= n;
        CH376_Cmd_End();

        CH376_Cmd(CMD_BYTE_WR_GO); /* 0x3d */
        status = wait();
        if (status == USB_INT_SUCCESS && remaining == 0) { /* 0x14 完成 */
            ESP_LOGI(TAG, "写入成功(%u字节)", len);
            return 1;
        }
        /* status==0x1E 表示还有缓冲，继续下一批 */
    } while (status == USB_INT_DISK_WRITE && remaining > 0);

    ESP_LOGE(TAG, "写入失败: 0x%02X", status);
    return 0;
}


int test_normal_write_completes(void){uint8_t data[3]={1,2,3};return CH376T_BYTE_WRITE_DATA(data,3)!=1||requests!=1||go_commands!=1||wait_calls!=2;}
int test_zero_request_repeats_until_external_error(void){scenario=1;uint8_t data[3]={1,2,3};uint8_t ok=CH376T_BYTE_WRITE_DATA(data,3);return ok!=0||requests!=4||go_commands!=4||wait_calls!=5||writes!=2;}
int test_oversize_request_is_clamped_and_accepted(void){scenario=2;uint8_t data[3]={1,2,3};uint8_t ok=CH376T_BYTE_WRITE_DATA(data,3);return ok!=1||requests!=1||go_commands!=1||writes!=5;}
