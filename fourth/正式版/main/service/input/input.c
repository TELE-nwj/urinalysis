/*
 * service 层：按键服务 —— app 层唯一的人机输入入口。
 *
 * 【为什么要这一层】
 *   按键接在哪个 GPIO、按下是低电平还是高电平、消抖几次、什么时候算一次短按，
 *   全是硬件细节，只应该存在于 driver/key。app 真正关心的只有
 *   "用户按的是 1 号键还是 2 号键"。本模块把驱动事件翻译成 app 自己的语义枚举
 *   再回调出去，app 因此不必 include 任何 driver 头文件，也就不会在改板、
 *   改引脚时被牵连。这是"底层与应用层接口分开"的落点之一。
 *
 * 【本层只做翻译 + 转发】
 *   采样和消抖不在这里重复实现，直接调 key_scan()；本层不额外打日志。
 */
#include "input.h"
#include "driver/key/key.h"
#include "esp_log.h"
#include <stddef.h>

static const char *TAG = "input";

static input_cb_t s_cb    = NULL;   /* app 登记的回调，可为 NULL(不要事件) */
static int        s_ready = 0;      /* key_init 是否成功 */

/*
 * 适配函数：把 driver 的 key_event_t 翻译成 service 的 input_key_t。
 * 【它存在的意义】driver 只知道自己那套事件枚举，app 只认自己那套；
 *   两边的枚举名一旦耦合，driver 改枚举就会波及 app。翻译只写在这一个函数里，
 *   以后 driver 增加事件(如长按)，也只在这里决定"要不要往上传"。
 */
static void key_event_adapter(key_event_t ev)
{
    if (!s_cb) return;              /* 未登记回调：事件丢弃 */

    switch (ev) {
    case KEY1_PRESS:  s_cb(INPUT_KEY1); break;
    case KEY2_PRESS:  s_cb(INPUT_KEY2); break;
    default:          break;        /* KEY_NONE 等非"短按"事件：不上传 */
    }
}

err_t input_init(input_cb_t cb)
{
    s_cb    = cb;
    s_ready = 0;

    /* 即使 cb 为 NULL 也要把 GPIO 配好：回调只是"登记"，可以后补；
       按 input.h 的约定，不注册也必须正常返回。 */
    if (key_init(key_event_adapter) != ERR_OK) {
        ESP_LOGE(TAG, "按键驱动初始化失败，输入服务不可用");
        return ERR_FAIL;
    }

    s_ready = 1;
    ESP_LOGI(TAG, "输入服务就绪: 2 键短按, 回调%s", cb ? "已登记" : "未登记");
    return ERR_OK;
}

void input_poll(void)
{
    /* 初始化失败时 GPIO 并未配置成功，读到的电平没有意义，可能误报按键
       (app 拿它当"开始/复位"，误报代价高)，所以这里空转不扫描。 */
    if (!s_ready) return;

    /* 纯转发：采样、消抖、短按判定都在 driver 内完成，本层不打日志。 */
    key_scan();
}
