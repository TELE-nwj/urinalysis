/* ============================================================
 * 入口层（组合根）：只做初始化编排与任务启动，不写业务细节。
 *
 * 分层：main → app → service → driver
 *   - service: 时钟/输入/尿量/屏显/存储/电量 —— app 层唯一可见的接口面
 *   - driver : 硬件(drop / screen_uart / ch376 / time_svc / walltime / battery)
 *   - app    : 三态状态机（待开始 / 测量中=S1 / 暂停=S2）
 *
 * 【为什么这里一个 driver 头文件都不 include】
 *   本文件是"组合根"，只调 service 层的 init；各模块自己的硬件初始化由对应
 *   service 在自己的 init 内部完成（例如 screen_init 里初始化 UART、
 *   power_init 里初始化 ADC）。这样入口与应用层都不会散落驱动细节。
 * ============================================================ */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/err.h"

/* 服务层 */
#include "service/clock/clock.h"
#include "service/power/power.h"
#include "service/screen/screen.h"
#include "service/storage/storage.h"
#include "service/urine/urine.h"

/* 应用层 */
#include "app/monitor/monitor.h"
#include "app/mobile/mobile.h"
#include "esp_mac.h"
#include <stdio.h>

static const char *TAG = "main";

void app_main(void)
{
    /* 启动标识：编译日期+时间。用来确认板子上跑的到底是哪一次编译的固件
       (IDE 的"当前工程"曾经指向参考工程 F:\ESP32\niaio\project，别烧错了) */
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "尿量计启动  分层版  编译于 %s %s", __DATE__, __TIME__);
    ESP_LOGI(TAG, "功能: 两键三态 + 串口屏 + 每分钟写U盘 + 电量/时间");
    ESP_LOGW(TAG, "[写盘握手修复版] 20261015-E；数据交接不插入诊断命令/FAT预检/完成后校验与关闭回读/保留会话保护；仍需实机验收");
    ESP_LOGI(TAG, "=================================================");

    /* ---- service 初始化（底层硬件在各自 init 内部完成） ---- */
    clock_init();          /* 时基 + 墙上时间(未对时从 00:00:00 起算) */
    urine_capture_init();  /* 液滴采集 GPIO 与中断 */

    if (screen_init() != ERR_OK) {          /* 串口屏 UART + 首页与初值 */
        ESP_LOGE(TAG, "串口屏初始化失败，停止启动");
        return;
    }
    if (power_init() != ERR_OK) {           /* 电池 ADC 分压采样 */
        ESP_LOGW(TAG, "电量检测初始化失败，继续运行(电量显示 --)");
    }
    if (storage_init() != ERR_OK) {         /* U盘: 进主机模式+检测磁盘 */
        ESP_LOGW(TAG, "存储未就绪(可能没插U盘), 仍继续运行");
    }

    /* 在唯一的状态属主启动之前，先注册永久完成回调适配器。 */
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    static char device_id[32];
    snprintf(device_id, sizeof(device_id), "NIAIO-%02X%02X%02X", mac[3], mac[4], mac[5]);
    const app_mobile_config_t mobile_config = {
        .device_id = device_id,
        .ap_ssid = CONFIG_NIAIO_AP_SSID,
        .ap_password = CONFIG_NIAIO_AP_PASSWORD,
        .ap_channel = 6,
        .ap_max_connections = 1,
    };
    const err_t mobile_init_result = app_mobile_init(&mobile_config);
    if (mobile_init_result != ERR_OK) ESP_LOGE(TAG, "手机适配器初始化失败: %ld", (long)mobile_init_result);

    /* ---- app 启动：按键与状态机 ---- */
    if (app_monitor_start() != ERR_OK) {
        ESP_LOGE(TAG, "按键/状态机启动失败");
        return;
    }
    if (mobile_init_result == ERR_OK && app_mobile_start() != ERR_OK) {
        ESP_LOGW(TAG, "手机网络启动失败，本地测量仍可使用");
    }

    /* 主循环空转；按键、测量、状态转换，以及每秒的电量/时间推送，
       全部由 monitor 任务处理。 */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
