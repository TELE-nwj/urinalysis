#include "drop.h"
#include "bsp/pin_map.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "driver/gpio.h"

static const char *TAG = "drop";

/* 保护共享变量的自旋锁 */
static portMUX_TYPE s_spin = portMUX_INITIALIZER_UNLOCKED;

/* ===== 环形间隔缓冲 Time[]：下标 1..255 存放间隔(秒)，弃用 Time[0] ===== */
static uint8_t  s_time[DROP_RING_SIZE];
static uint8_t  s_ring_pos = 1;    /* 下一个要写入的槽(1..255)，循环使用 */
static uint8_t  s_newest   = 0;    /* 最近一次写入的槽(0=尚无) */
static volatile uint8_t s_win_done = 0;  /* 窗口完成事件 */

/* ISR(生产者)→poll(消费者) 传递"相邻间隔(us)"的小队列 */
#define QUEUE_SIZE  32
static uint64_t    s_q[QUEUE_SIZE];
static volatile uint16_t s_q_head = 0;
static volatile uint16_t s_q_tail = 0;

static volatile uint32_t  s_total          = 0; /* 累计有效滴数(永不清零) */
static volatile uint64_t  s_prev_drop_us   = 0; /* 上一有效滴时刻 */
static volatile uint64_t  s_last_interval_us = 0;

/* 中断服务：只做极短操作(消抖+入队+计数)，绝不做耗时的写入分析 */
static void IRAM_ATTR drop_isr(void *arg)
{
    (void)arg;
    uint64_t now = esp_timer_get_time();

    /* 整个处理过程都在自旋锁内：
     * s_prev_drop_us 是 64 位量，任务侧的 drop_reset_interval() 也会把它写 0，
     * 双核上若在锁外读，可能读到"半新半旧"的撕裂值，导致消抖判断与入队时间差
     * 算错(偶发多计/漏计一滴)。消抖只是一次比较，放进锁内的开销可以忽略。 */
    portENTER_CRITICAL_ISR(&s_spin);

    uint64_t prev = s_prev_drop_us;

    /* 消抖：距上一有效滴 < 0.5s 视为抖动 */
    if (prev != 0 && (now - prev) < DROP_DEBOUNCE_US) {
        portEXIT_CRITICAL_ISR(&s_spin);
        return;
    }

    s_total++;                       /* 有效滴计数(永不清零) */
    if (prev != 0) {                 /* 有上一滴 → 得到一个有效相邻间隔 */
        uint16_t next = (s_q_head + 1) % QUEUE_SIZE;
        if (next != s_q_tail) {      /* 队未满才入队 */
            s_q[s_q_head] = now - prev;
            s_q_head = next;
        }
    }
    s_prev_drop_us = now;
    portEXIT_CRITICAL_ISR(&s_spin);
}

err_t drop_init(void)
{
    gpio_config_t cfg = {
        .intr_type      = GPIO_INTR_POSEDGE,
        .mode           = GPIO_MODE_INPUT,
        .pin_bit_mask   = (1ULL << BSP_PIN_DROP_DETECT),
        .pull_down_en   = GPIO_PULLDOWN_ENABLE,
        .pull_up_en     = GPIO_PULLUP_DISABLE,
    };
    if (gpio_config(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "GPIO 配置失败");
        return ERR_FAIL;
    }
    /* ISR 服务可能已被别的模块先装过：那种情况返回 ESP_ERR_INVALID_STATE，属正常；
       其它错误必须报出来——否则中断装不上会表现为"一滴都收不到"却毫无线索。 */
    esp_err_t e = gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "GPIO ISR 服务安装失败: %s", esp_err_to_name(e));
        return ERR_FAIL;
    }
    /* 挂上滴落中断；失败同样是"静默收不到滴"，必须报错返回。 */
    e = gpio_isr_handler_add(BSP_PIN_DROP_DETECT, drop_isr, NULL);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "GPIO 中断处理函数注册失败: %s", esp_err_to_name(e));
        return ERR_FAIL;
    }
    ESP_LOGI(TAG, "液滴 ISR 挂载于 GPIO%d (方式A, 消抖=%.1fs)",
             BSP_PIN_DROP_DETECT, DROP_DEBOUNCE_US / 1000000.0);
    return ERR_OK;
}

void drop_poll(void)
{
    for (;;) {
        uint64_t us;
        portENTER_CRITICAL(&s_spin);
        if (s_q_tail == s_q_head) {          /* 队空 */
            portEXIT_CRITICAL(&s_spin);
            break;
        }
        us = s_q[s_q_tail];
        s_q_tail = (s_q_tail + 1) % QUEUE_SIZE;
        portEXIT_CRITICAL(&s_spin);

        /* 换算成秒(饱和 255)写入 Time[] */
        uint32_t sec = (uint32_t)(us / 1000000ULL);
        if (sec > 255) sec = 255;

        s_last_interval_us = us;
        uint8_t pos = s_ring_pos;
        s_time[pos] = (uint8_t)sec;
        s_newest   = pos;

        if (pos == DROP_WIN_SIZE) {          /* 攒满一轮(255 个间隔) */
            s_ring_pos = 1;                  /* 下一轮回到 Time[1] */
            s_win_done = 1;                  /* 通知上层跑一次方式A缺水判定 */
        } else {
            s_ring_pos = (uint8_t)(pos + 1);
        }
    }
}

void drop_irq_disable(void)
{
    gpio_intr_disable(BSP_PIN_DROP_DETECT);
}

void drop_irq_enable(void)
{
    /* 关中断期间可能攒下的电平边沿没被记录，这里重新使能即可 */
    gpio_intr_enable(BSP_PIN_DROP_DETECT);
}

void drop_reset_interval(void)
{
    portENTER_CRITICAL(&s_spin);
    s_prev_drop_us = 0;
    s_q_tail = s_q_head;
    portEXIT_CRITICAL(&s_spin);
    s_last_interval_us = 0;
    s_ring_pos = 1;
    s_newest = 0;
    s_win_done = 0;
}

uint32_t drop_total(void)
{
    return s_total;
}

uint8_t drop_interval_sec(uint16_t i)
{
    return (i >= 1 && i < DROP_RING_SIZE) ? s_time[i] : 0;
}

uint16_t drop_newest(void)
{
    return s_newest;
}

uint64_t drop_last_interval_us(void)
{
    return s_last_interval_us;
}

uint64_t drop_last_time_us(void)
{
    uint64_t v;
    portENTER_CRITICAL(&s_spin);      /* 64位变量读要保护(ISR 里也在写) */
    v = s_prev_drop_us;
    portEXIT_CRITICAL(&s_spin);
    return v;
}

uint8_t drop_window_done(void)
{
    uint8_t r;
    portENTER_CRITICAL(&s_spin);
    r = s_win_done;
    s_win_done = 0;
    portEXIT_CRITICAL(&s_spin);
    return r;
}
