
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define CMD_BYTE_WRITE 0x3c
#define CMD_WR_REQ_DATA 0x2d
#define CMD_BYTE_WR_GO 0x3d
#define CMD_FILE_CLOSE 0x36
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
static bool force_block;
static bool force_feed_block, force_finish_block, force_write_fault;
static unsigned feed_calls, data_requests, write_goes, last_parameter;
static void event(unsigned v) { hash = (hash ^ v) * 16777619u; }
void CH376_Cmd(uint8_t cmd) { if(cmd==0x2d)data_requests++;if(cmd==0x3d)write_goes++;event(0x100u | cmd); }
void CH376_Write(uint8_t value) { last_parameter=value;event(0x200u | value); }
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
bool CH376_DiagCanWrite(const char *stage) { (void)stage; return !force_block; }
bool CH376_DiagCanFeed(const char *stage) { (void)stage;feed_calls++;return !force_feed_block; }
bool CH376_DiagCanFinish(void) {return !force_finish_block;}
bool CH376_DiagWriteFault(void) { return force_write_fault; }
#define wait mock_wait
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

uint8_t CH376T_FILE_CLOSE(void)
{
    uint8_t status;
    CH376_Cmd(CMD_FILE_CLOSE);//0x36
    CH376_Write(0x01);   /* 1 = 允许自动更新文件长度(否则写盘长度不更新) */
    CH376_Cmd_End();
    // 等待中断
    status = wait();
    // 检查状态
    if (status == 0x14)
    {  // 说明：USB_INT_SUCCESS
        ESP_LOGI(TAG, "关闭文件成功");
        return 1;
    }
    else 
    {
        ESP_LOGE(TAG, "关闭文件失败，状态码: 0x%02X", status);
        return 0;
    }
}




#undef wait
uint8_t wait(void)
{
    uint32_t timeout_counter = 0;
    /* 超时上限：DISK_MOUNT 挂载大容量U盘可能耗时较久，放宽到 ~7s */
    const uint32_t timeout_max = 500000;
// 轮询等待中断（INT引脚拉低）
    ESP_LOGI(TAG, "等待CH376T中断响应...");
    timeout_counter = 0;
    while (gpio_get_level(CH376_INT_GPIO) == 1) {
        // 简单延时
        esp_rom_delay_us(10);  // 10微秒延时
        timeout_counter++;
        // 周期让出 CPU，避免饿死空闲任务/看门狗(仅任务上下文调用)
        if ((timeout_counter & 0xFFu) == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        
        // 超时检测
        if (timeout_counter > timeout_max) {
            ESP_LOGE(TAG, "等待中断超时！");
            CH376_Cmd_End();
            return 0xff;
        }
        
        // 每10000次计数打印一次调试信息
        if (timeout_counter % 50000 == 0) {
            ESP_LOGI(TAG, "仍在等待中断... 计数: %lu", timeout_counter);
        }
    }
    ESP_LOGI(TAG, "检测到中断，INT引脚已拉低");
    return CH376_GetStatus();
}


unsigned test_normal(void) { uint8_t b[3] = {1,2,3}; event(CH376T_BYTE_WRITE_DATA(b,3)); return hash; }
unsigned test_zero_progress(void) { scenario=1; return test_normal(); }
unsigned test_oversize(void) { scenario=2; return test_normal(); }
unsigned test_multiblock(void) { scenario=3; return test_normal(); }
unsigned test_request_error(void) { scenario=4; return test_normal(); }
unsigned test_wait_immediate(void) { return wait() != 0x14 || delay_calls || calls != 1; }
unsigned test_wait_delayed(void) { target_polls=600; return wait() != 0x14 || delay_calls != 600 || yields != 2 || calls != 1; }
unsigned test_wait_timeout(void) { target_polls=600000; return wait() != 0xff || delay_calls != 500001 || calls || yields != (500001u/256u); }
unsigned test_guard_blocks_write(void) { force_block=true; uint8_t b[3]={1,2,3}; return CH376T_BYTE_WRITE_DATA(b,3)!=0 || hash!=0; }
unsigned test_no_diagnostic_call_during_data_handoff(void) {force_feed_block=true;uint8_t b[3]={1,2,3};return CH376T_BYTE_WRITE_DATA(b,3)!=1||feed_calls||data_requests!=1||write_goes!=1;}
unsigned test_finish_guard_rejects_success(void) {force_finish_block=true;uint8_t b[3]={1,2,3};return CH376T_BYTE_WRITE_DATA(b,3)!=0;}
unsigned test_fault_close_does_not_update_length(void) {force_write_fault=true;calls=1;return CH376T_FILE_CLOSE()!=1||last_parameter!=0;}
