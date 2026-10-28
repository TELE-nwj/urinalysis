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

static const char *TAG = "main";

void app_main(void)
{
    /* 【轻量化：日志降噪】先把噪声最大的两个模块降到 WARN。
     * 原因：CH376 的 wait() 每次调用要打 3 行日志（"等待中断"/"检测到中断"/"中断代码"），
     * 整条初始化链累计 35~45 行；每行在 115200 波特下约 5~6ms，合计 200~270ms 全压在启动上。
     * screen 每发一帧也打一行，运行期约 23 行/秒。
     * 这两类日志日常使用价值低，降级后：
     *   · 启动快约 250~350ms
     *   · 写盘事务不再把日志挤在"关闭液滴中断"的窗口里（窗口短一半，丢滴也少一半）
     * 其它模块（main/S1/monitor/storage）的日志保持 INFO 不变。 */
    esp_log_level_set("CH376T", ESP_LOG_WARN);
    esp_log_level_set("screen", ESP_LOG_WARN);

    /* 启动标识：编译日期+时间。用来确认板子上跑的到底是哪一次编译的固件
       (IDE 的"当前工程"曾经指向参考工程 F:\ESP32\niaio\project，别烧错了) */
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "尿量计启动  分层版  编译于 %s %s", __DATE__, __TIME__);
    ESP_LOGI(TAG, "功能: 两键三态 + 串口屏 + 每分钟写U盘 + 电量/时间");
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

    /* U盘: 进主机模式 + 检测磁盘 + FAT 预检。
     *
     * 【为什么必须在 app_monitor_start() 之前】
     *   这条链里全是 CH376 的软件模拟 SPI 事务（位翻转 + 延时），一次
     *   DISK_MOUNT 在 U 盘未插时最坏要等到 wait() 超时。它和 monitor 任务
     *   共用同一颗 CH376，而软 SPI 没有互斥保护 —— 如果放到 monitor 启动
     *   之后再跑，用户此时按键开始测量会触发 S1 侧的 storage_init，
     *   两个任务同时操作 SPI 会把命令序列打乱，可能写坏 U 盘。
     *   所以这里宁可让启动多等一段确定的时间，也不能引入并发。
     *   （初始化阶段已设短超时，U 盘没插时不会死等 7 秒。） */
    if (storage_init() != ERR_OK) {
        ESP_LOGW(TAG, "存储未就绪(可能没插U盘), 仍继续运行");
    }

    /* ---- app 启动：按键与状态机 ---- */
    if (app_monitor_start() != ERR_OK) {
        ESP_LOGE(TAG, "按键/状态机启动失败");
        return;
    }

    /* 主循环空转；按键、测量、状态转换，以及每秒的电量/时间推送，
       全部由 monitor 任务处理。 */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
