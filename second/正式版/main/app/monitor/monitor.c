#include "monitor.h"
#include "app/S1/S1.h"
#include "app/S2/S2.h"
#include "app/S3/S3.h"
#include "app/S4/S4.h"
#include "driver/drop/drop.h"
#include "driver/time_svc/time_svc.h"
#include "service/screen/screen.h"
#include "service/storage/storage.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "monitor";

/* 主循环周期(ms) */
#define LOOP_MS   20

/* 当前状态(唯一的"状态判断"依据) */
static app_state_t s_state = APP_S1;

/* 最近一次登录的用户名(仅供日志显示) */
static char s_user[SCREEN_USERNAME_MAX + 1] = "";

/* ---- 名字表(下标 = 枚举值，省掉一堆 switch) ---- */
static const char *const STATE_NAME[] = {"?", "S1待登录", "S2已登录", "S3测量中", "S4暂停"};
static const char *const CMD_NAME[]   = {"?", "login", "start", "stop", "reset"};

static const char *state_name(app_state_t s)
{
    return ((int)s >= 1 && (int)s <= 4) ? STATE_NAME[(int)s] : "?";
}

static const char *cmd_name(screen_cmd_t c)
{
    return ((int)c >= 0 && (int)c <= 4) ? CMD_NAME[(int)c] : "?";
}

/* ---- 【调试】当前信息快照 ---- */
void app_monitor_dump(const char *note)
{
    const urine_meas_t *m = S3_meas();
    uint64_t last_us = drop_last_interval_us();

    ESP_LOGI(TAG,
             "[%s] 状态=%s 用户='%s' 尿量=%.1fml 速率=%.1fml/h 滴数=%lu "
             "最近间隔=%lums 干=%d 满=%d",
             note ? note : "快照",
             state_name(s_state),
             s_user,
             (double)m->total_ml,
             (double)m->rate_mlh,
             (unsigned long)m->total_drops,
             (unsigned long)(last_us / 1000u),
             (int)m->flag_dry,
             (int)m->flag_bag_full);
}

/* ---- 进入状态(唯一入口): 只负责"进新状态" ---- */
static void goto_state(app_state_t s)
{
    if (s == s_state) return;

    ESP_LOGI(TAG, "状态切换 %s -> %s", state_name(s_state), state_name(s));
    s_state = s;

    if (s_state == APP_S1) {
        s_user[0] = '\0';       /* 回到"待登录": 清当前用户，允许换用户 */
    }

    switch (s_state) {
    case APP_S1: S1_enter(); break;
    case APP_S2: S2_enter(); break;
    case APP_S3: S3_enter(); break;
    case APP_S4: S4_enter(); break;
    }
}

/* ============================================================
 * 【唯一】命令 → 状态 的转移函数
 *   解析结果 (cmd, user) 进来，就这一张 switch 决定：
 *     做什么(调 S3 的状态内动作) + 去哪个状态(goto_state)
 *
 *   转移表：
 *     login  仅 S1 有效           → S2
 *     start  S2 开始 / S4 继续    → S3
 *     stop   S3                   → S4
 *     reset  任意状态/任意用户    → S1 (并自动关闭当前用户文件)
 * ============================================================ */
static void on_cmd(screen_cmd_t cmd, const char *user)
{
    const char *u   = (user && *user) ? user : "";
    uint64_t    now = timesvc_now_ms();

    ESP_LOGI(TAG, ">>>>>>>>>> 收到屏幕命令: %s  用户='%s' <<<<<<<<<<", cmd_name(cmd), u);
    app_monitor_dump("收到命令时");

    /* login / reset 不校验用户名：
       · login → 允许换用户
       · reset → 不管当前登录的是谁，收到就复位 */
    if (cmd != SCREEN_CMD_LOGIN && cmd != SCREEN_CMD_RESET &&
        strcmp(u, s_user) != 0) {
        ESP_LOGW(TAG, "用户名不匹配(当前='%s' 收到='%s'), 忽略 %s",
                 s_user, u, cmd_name(cmd));
        app_monitor_dump("处理完命令后");
        return;
    }

    switch (cmd) {
    /* ---------- login: 仅 S1 有效 ---------- */
    case SCREEN_CMD_LOGIN:
        if (s_state != APP_S1) {
            ESP_LOGW(TAG, "%s 忽略 login(换用户需先 reset)", state_name(s_state));
            break;
        }
        if (!*u) {
            ESP_LOGW(TAG, "login 用户名为空，忽略");
            break;
        }
        /* U盘: 登录只"创建文件 + 写表头"，正式打开在 start 时做 */
        if (storage_create_user(u) != ERR_OK) {
            ESP_LOGW(TAG, "创建用户文件失败(U盘未就绪?)，仍允许登录");
        }
        snprintf(s_user, sizeof(s_user), "%s", u);
        ESP_LOGI(TAG, "登录成功: '%s'", s_user);
        goto_state(APP_S2);
        break;

    /* ---------- start: S2 开始 / S4 继续 → S3 ---------- */
    case SCREEN_CMD_START:
        if (s_state == APP_S2) {
            S3_reset_data(now);     /* 全新测量: 清数据/计时归零/记第0分钟 */
            S3_file_open(s_user);   /* 打开用户文件(追加模式), 保持打开 */
            goto_state(APP_S3);
        } else if (s_state == APP_S4) {
            S3_resume(now);         /* 续测: 只重置计时基准 */
            S3_file_open(s_user);   /* 再次打开文件续写 */
            goto_state(APP_S3);
        } else {
            ESP_LOGW(TAG, "%s 忽略 start", state_name(s_state));
        }
        break;

    /* ---------- stop: S3 → S4 ---------- */
    case SCREEN_CMD_STOP:
        if (s_state == APP_S3) {
            S3_pause(now);          /* 结算计时 + 把数值冻结到屏 */
            S3_file_close();        /* 暂停: 关闭用户文件 */
            goto_state(APP_S4);
        } else {
            ESP_LOGW(TAG, "%s 忽略 stop", state_name(s_state));
        }
        break;

    /* ---------- reset: 任意状态、任意用户都能复位 → S1 ---------- */
    case SCREEN_CMD_RESET:
        ESP_LOGI(TAG, "%s 复位: 关闭文件 + 清空数据 → S1", state_name(s_state));

        S3_file_close();         /* 复位: 关闭当前用户文件 */
        S3_clear(now);           /* 清测量数据/计时 */
        S3_push_values();        /* 屏上 4 个数值归零 */
        goto_state(APP_S1);      /* → S1(goto_state 内部会清空用户名) */
        break;

    default:
        break;
    }

    app_monitor_dump("处理完命令后");
}

/* ---- 主循环: 只有 S3(测量中) 有周期动作 ---- */
static void monitor_task(void *arg)
{
    (void)arg;
    for (;;) {
        switch (s_state) {
        case APP_S3: S3_run(); break;   /* 采集 + 解算 + 推屏 */
        default:     break;             /* S1/S2/S4: 无周期动作 */
        }
        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
    }
}

/* ---- 启动 ---- */
err_t app_monitor_start(void)
{
    screen_set_cmd_cb(on_cmd);

    s_user[0] = '\0';
    s_state   = APP_S1;
    S1_enter();

    BaseType_t ok = xTaskCreate(monitor_task, "monitor", 4096, NULL, 5, NULL);
    return (ok == pdPASS) ? ERR_OK : ERR_FAIL;
}

void app_monitor_reset(void)
{
    S3_clear(timesvc_now_ms());
    S3_push_values();
    goto_state(APP_S1);
}

const urine_meas_t *app_monitor_get(void)
{
    return S3_meas();
}
 