#include "S3.h"
#include "service/screen/screen.h"
#include "service/storage/storage.h"
#include "driver/drop/drop.h"
#include "driver/time_svc/time_svc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "S3";

/* CH376 驱动内部的日志 TAG —— 写盘临界区里要临时把它屏蔽掉 */
#define CH376_LOG_TAG   "CH376T"

/* ===== S3 测量中：测量期数据 =====
 * 本状态只负责"把测量结果写到串口屏" + "每分钟往 U盘写一行"。
 * 写盘相关实现见 service/storage。 */
static urine_meas_t s_meas;

/* 会话计时：测量中累加；暂停冻结、复位归零 */
static uint64_t s_elapsed_ms = 0;
static uint64_t s_acc_tick   = 0;   /* 上一次记账时刻 */
static uint32_t s_last_min   = 0;   /* 已记录的分钟序号 */

/* 推屏节流：显示屏不需要 50Hz，200ms 一次足够
   (monitor 20ms 一轮 → 实际约 5Hz，避免把串口屏刷爆/卡顿) */
#define S3_UI_PERIOD_MS   200u
static uint64_t s_ui_tick = 0;        /* 上次推屏时刻 */
static char     s_last_status[12];    /* 上次推给屏的状态文本(变了才推) */

/* ===== U盘记录(每分钟一条) =====
 * 文件不常开：写盘那一刻才 open→write→close，全程在临界区内。 */
static char s_user[SCREEN_USERNAME_MAX + 1] = ""; /* 本次测量写入哪个用户文件 */
static volatile bool s_ui_pause = false; /* true: 写盘临界区内, 暂停推屏 */
static float     s_last_ml   = 0.0f;  /* 上一分钟标记时的累计尿量(算增量用) */
static SemaphoreHandle_t s_spi_mtx = NULL; /* 串行化 CH376 SPI 访问(monitor/uart_rx 两个任务) */

/* ---- 临界区进出：关液滴中断 + 暂停推屏 + 屏蔽驱动日志 + 拿 SPI 锁 ---- */
static void S3_crit_enter(void)
{
    if (!s_spi_mtx) {
        s_spi_mtx = xSemaphoreCreateMutex();
    }
    if (s_spi_mtx) {
        xSemaphoreTake(s_spi_mtx, portMAX_DELAY);
    }

    esp_log_level_set(CH376_LOG_TAG, ESP_LOG_NONE);  /* 区内不许有日志 */
    drop_irq_disable();                              /* 停液滴中断 */
    s_ui_pause = true;                               /* 停串口屏推送 */
}

static void S3_crit_exit(void)
{
    s_ui_pause = false;
    drop_irq_enable();                               /* 恢复液滴中断 */
    esp_log_level_set(CH376_LOG_TAG, ESP_LOG_INFO);

    if (s_spi_mtx) {
        xSemaphoreGive(s_spi_mtx);
    }
}

/* 当前状态文本(报警优先级) */
static const char *cur_status(void)
{
    if (s_meas.flag_bag_full) return "BAG FULL";
    if (s_meas.flag_dry)      return "DRY";      /* 方式A缺水/断流 */
    return "READY";
}

/* 把会话计时格式化成 hh:mm:ss */
static void fmt_elapsed(char *buf, size_t sz)
{
    uint32_t sec = (uint32_t)(s_elapsed_ms / 1000);
    snprintf(buf, sz, "%02lu:%02lu:%02lu",
             (unsigned long)(sec / 3600),
             (unsigned long)((sec % 3600) / 60),
             (unsigned long)(sec % 60));
}

/* 只推 4 个"数值/时间"控件(不带状态)
 * —— 离开 S3 时冻结数据、复位时归零，都用它 */
void S3_push_values(void)
{
    char ts[16];
    screen_show_urine(s_meas.total_ml);        /* ① 累计尿量 ml   -> data.n0.txt */
    screen_show_rate(s_meas.rate_mlh);         /* ② 实时速率 ml/h -> data.n1.txt */
    screen_show_drops(s_meas.total_drops);     /* ③ 累计滴数      -> data.n2.txt */
    fmt_elapsed(ts, sizeof(ts));               /* ④ 会话计时      -> data.t1.txt */
    screen_show_time(ts);
}

/* 把当前解算结果推送到串口屏(5 个量)；状态文本变了才推，避免反复刷同一个值 */
void S3_push_ui(void)
{
    const char *st;

    if (s_ui_pause) return;            /* 写盘临界区内: 不碰串口屏 */

    S3_push_values();

    st = cur_status();                         /* ⑤ 状态/报警    -> data.t0.txt */
    if (strcmp(st, s_last_status) != 0) {
        snprintf(s_last_status, sizeof(s_last_status), "%s", st);
        screen_show_status(st);
    }
}

/* ============================================================
 * U盘记录：每分钟一条
 *   内容: 时间(hh:mm:ss)  本分钟尿量(ml)  本分钟平均流速(ml/h)
 *   本分钟尿量 = 本分钟末累计总量 - 上一分钟标记时的累计总量
 *   平均流速   = 本分钟尿量 × 60
 *
 *   触发时进入临界区: 停液滴中断 + 暂停推屏 + 屏蔽驱动日志 + 拿SPI锁
 *   临界区内【只做 SPI 通讯，不打印任何日志】
 * ============================================================ */
static void S3_record_write(uint32_t minute)
{
    char   c1[16];                                /* 时间 hh:mm:ss */
    char   c2[16];                                /* 本分钟尿量   */
    char   c3[16];                                /* 本分钟平均流速 */
    char   line[80];
    uint32_t sec = minute * 60u;
    float  delta = s_meas.total_ml - s_last_ml;   /* 本分钟尿量增量(ml) */
    float  rate  = delta * 60.0f;                 /* 本分钟平均流速(ml/h) */
    err_t  e;

    s_last_ml = s_meas.total_ml;                  /* 消费掉这一分钟的增量基准 */

    /* 三列各自成串，再按 storage.h 里的列宽/间隔【右对齐】拼成一行 */
    snprintf(c1, sizeof(c1), "%02lu:%02lu:%02lu",
             (unsigned long)(sec / 3600),
             (unsigned long)((sec % 3600) / 60),
             (unsigned long)(sec % 60));
    snprintf(c2, sizeof(c2), "%.1fml", (double)delta);
    snprintf(c3, sizeof(c3), "%.1fml/h", (double)rate);

    int n = snprintf(line, sizeof(line), REC_ROW_FMT, c1, c2, c3);
    if (n < 0 || n >= (int)sizeof(line) - 2) {
        ESP_LOGW(TAG, "第%lu分钟: 记录字符串过长，跳过", (unsigned long)minute);
        return;
    }

    if (!*s_user) {
        ESP_LOGW(TAG, "第%lu分钟 %s —— 未指定用户(未开始测量)，跳过写盘",
                 (unsigned long)minute, line);
        return;
    }

    /* ---- 进临界区前打一条(带上本条内容，便于核对) ---- */
    ESP_LOGI(TAG, ">>>>> 进入临界区(停液滴中断/停推屏): 第%lu分钟 %s",
             (unsigned long)minute, line);

    line[n++] = '\r';                             /* 行尾 */
    line[n++] = '\n';
    line[n]   = '\0';

    S3_crit_enter();
    /* ============ 临界区内: 只有 SPI 通讯 ============ */
    e = storage_open_user(s_user);       /* 打开 + 定位到文件末尾(追加) */
    if (e == ERR_OK) {
        e = storage_write_open(line);    /* 写这一行 */
        storage_close_user();            /* ★ 立刻关闭: 目录项落盘，拔盘最多丢这一分钟 */
    }
    /* ============================================== */
    S3_crit_exit();

    if (e != ERR_OK) {
        ESP_LOGW(TAG, "<<<<< 退出临界区: 第%lu分钟写盘失败，已跳过",
                 (unsigned long)minute);
    } else {
        ESP_LOGI(TAG, "<<<<< 退出临界区: 第%lu分钟已写入U盘",
                 (unsigned long)minute);
    }
}

/* ===== U盘文件开关(monitor 调用) =====
 * 文件不常开：这里只记住/清掉用户名，并在开始时做一次介质检查
 * (介质检查 0x30 会打断文件上下文，所以必须在"没有文件打开"时做)。 */

err_t S3_file_open(const char *user)
{
    err_t e;

    if (!user || !*user) return ERR_PARAM;

    snprintf(s_user, sizeof(s_user), "%s", user);

    ESP_LOGI(TAG, ">>>>> 进入临界区: 确认用户文件 '/%s.txt'", s_user);

    S3_crit_enter();
    /* ============ 临界区内: 只有 SPI 通讯 ============ */
    /* 顺便做介质检查(拔插过就重挂载) + 保证文件存在(不存在则建+写表头) */
    e = storage_create_user(s_user);
    /* ============================================== */
    S3_crit_exit();

    if (e != ERR_OK) {
        ESP_LOGW(TAG, "<<<<< 退出临界区: 用户文件不可用(U盘未就绪?)，本分钟起将跳过写盘");
    } else {
        ESP_LOGI(TAG, "<<<<< 退出临界区: 本次测量将写入 '/%s.txt'(每分钟写完即关)", s_user);
    }
    return e;
}

void S3_file_close(void)
{
    if (!*s_user) return;

    ESP_LOGI(TAG, "本次测量结束，不再写入: '/%s.txt'", s_user);
    s_user[0] = '\0';
}

void S3_reset_data(uint64_t now_ms)
{
    urine_meas_reset(&s_meas);      /* 会话从 0 计(重记测量起点) */
    s_elapsed_ms = 0;
    s_last_min   = 0;
    s_acc_tick   = now_ms;
    s_last_ml    = 0.0f;            /* 每分钟增量基准归零 */
}

void S3_update(uint64_t now_ms)
{
    /* ① 推进会话计时 */
    s_elapsed_ms += now_ms - s_acc_tick;
    s_acc_tick    = now_ms;

    /* ② 解算：累计/瞬时速率/方式A缺水/尿袋满(需上层先 drop_poll) */
    urine_update(&s_meas);

    /* ③ 每到新一分钟：往U盘写一条 */
    uint32_t mins = (uint32_t)(s_elapsed_ms / 60000);
    if (mins > s_last_min) {
        s_last_min = mins;
        S3_record_write(mins);      /* U盘: 每分钟一条 */
    }
}

void S3_clear(uint64_t now_ms)
{
    urine_meas_reset(&s_meas);
    s_elapsed_ms = 0;
    s_last_min   = 0;
    s_acc_tick   = now_ms;
    s_last_ml    = 0.0f;
}

void S3_resume(uint64_t now_ms)
{
    /* 暂停时已结算过计时, 继续时仅重置记账基准 */
    s_acc_tick = now_ms;
    /* 注意: s_last_ml / s_last_min 保持不变, 计时接着往下走,
       暂停期间落下的滴会计入恢复后的下一个整分钟 */
}

void S3_pause(uint64_t now_ms)
{
    /* 结算计时到暂停瞬间, 并把 4 个数值冻结在屏上(状态由 S4 刷成 PAUSE) */
    s_elapsed_ms += now_ms - s_acc_tick;
    s_acc_tick    = now_ms;
    S3_push_values();
}

const urine_meas_t *S3_meas(void)
{
    return &s_meas;
}

/* ===== 状态入口 ===== */
void S3_enter(void)
{
    ESP_LOGI(TAG, "进入 S3 测量中");

    /* 进来先整体刷一次屏：清空"上次状态"缓存强制推一次状态，
       让屏幕立刻和当前数据对齐(否则从 S4 回来时状态可能停在上一个状态的文本) */
    s_last_status[0] = '\0';
    S3_push_ui();
    s_ui_tick = timesvc_now_ms();
}

/* 每周期入口(monitor 20ms 调一次)：采集 → 解算 → 推屏(200ms 节流) */
void S3_run(void)
{
    uint64_t now = timesvc_now_ms();

    drop_poll();                        /* ① 采集: 把 ISR 里的滴间隔搬进 Time[] */
    S3_update(now);                     /* ② 解算: 计时/尿量/瞬时速率/缺水/满袋/按分钟快照 */

    /* ③ 推屏: 200ms 节流(20ms 周期下约 5Hz)，避免把串口屏喂爆 */
    if (now - s_ui_tick >= S3_UI_PERIOD_MS) {
        s_ui_tick = now;
        S3_push_ui();
    }
}
