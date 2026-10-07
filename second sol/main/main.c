/* ============================================================
 * 入口层：只做初始化与任务编排，不写业务细节。
 *
 * 分层：main → app → service → driver
 *   - driver : 硬件(drop/screen_uart/ch376/time_svc)
 *   - service: 串口屏显示、U盘存储、尿量解算
 *   - app    : 两键三态状态机
 * ============================================================ */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* driver */
#include "driver/drop/drop.h"
#include "driver/screen_uart/screen_uart.h"
#include "driver/time_svc/time_svc.h"

/* service */
#include "service/screen/screen.h"
#include "service/storage/storage.h"

/* app */
#include "app/monitor/monitor.h"

static const char *TAG = "main";

void app_main(void)
{
    /* 启动标识：编译日期+时间。用来确认板子上跑的到底是哪一次编译的固件
       (IDE 的"当前工程"曾经指向参考工程 F:\ESP32\niaio\project，别烧错了) */
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "尿量计启动  分层版  build %s %s", __DATE__, __TIME__);
    ESP_LOGI(TAG, "功能: 两键三态 + 串口屏显示 + 每分钟写U盘");
    ESP_LOGI(TAG, "=================================================");

    /* ---- driver 初始化 ---- */
    timesvc_init();          /* 底层取时 */
    if (drop_init() != ERR_OK) {
        ESP_LOGE(TAG, "液滴输入初始化失败，停止启动");
        return;
    }
    if (screen_uart_init() != ERR_OK) {
        ESP_LOGE(TAG, "串口屏 UART 初始化失败，停止启动");
        return;
    }

    /* ---- service 初始化 ---- */
    if (storage_init() != ERR_OK) {          /* U盘: 进主机模式+检测磁盘 */
        ESP_LOGW(TAG, "存储未就绪(可能没插U盘), 仍继续运行");
    }
    screen_init();           /* 串口屏服务: 只发送显示指令 */

    /* ---- app 启动 ---- */
    if (app_monitor_start() != ERR_OK) ESP_LOGE(TAG, "按键/状态机启动失败");

    /* 主循环空转；按键、测量和状态转换都由 monitor 任务处理。 */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
