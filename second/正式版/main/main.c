/* ============================================================
 * 入口层：只做初始化与任务编排，不写业务细节。
 *
 * 分层：main → app → service → driver
 *   - driver : 硬件(drop/screen_uart/ch376/time_svc)
 *   - service: 桥接/能力(screen 串口屏、storage U盘、urine 解算、record 记录)
 *   - app    : 业务状态机(monitor) —— 应用层重构中
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
    ESP_LOGI(TAG, "功能: 串口屏S1~S4 + 每分钟写U盘 + 速率7s无新滴归零");
    ESP_LOGI(TAG, "=================================================");

    /* ---- driver 初始化 ---- */
    timesvc_init();          /* 底层取时 */
    drop_init();             /* 液滴中断(方式A采集) */
    screen_uart_init();      /* 串口屏 UART 字节收发 */

    /* ---- service 初始化 ---- */
    if (storage_init() != ERR_OK) {          /* U盘: 进主机模式+检测磁盘 */
        ESP_LOGW(TAG, "存储未就绪(可能没插U盘), 仍继续运行");
    }
    screen_init();           /* 串口屏服务: 注册接收回调 + 初始界面 */

    /* ---- app 启动 ---- */
    app_monitor_start();

    /* 主循环空转：实际工作由 monitor(20ms) / uart_rx 两个任务承担
       (之前的“主循环直接读串口 / 每秒发 5 个数据”收发测试已完成，
        记录见 测试文档/串口屏收发测试记录.md) */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
