/* 入口层：只做初始化与任务编排，不写业务细节。 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "service/screen/screen.h"
#include "service/storage/storage.h"
#include "driver/drop/drop.h"
#include "driver/screen_uart/screen_uart.h"
#include "driver/battery/battery.h"
#include "driver/time_svc/time_svc.h"
#include "app/monitor.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "尿量计(分层v4) 启动");

    /* 各层初始化 */
    timesvc_init();                 /* driver: 时间(底层取时) */
    drop_init();                    /* driver: 液滴中断 */
    screen_uart_init();             /* driver: 串口屏 UART 通讯 */
    battery_init();                 /* driver: 电池(占位) */

    if (storage_init() != ERR_OK) { /* service: U盘(检测磁盘) */
        ESP_LOGW(TAG, "存储未就绪，仍继续运行");
    }

    screen_init();                  /* service: 串口屏服务(注册接收,显示初始界面) */
    app_monitor_start();            /* app: 主状态机/监控中枢 */

    /* 主循环仅空转，实际工作由各任务承担 */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
