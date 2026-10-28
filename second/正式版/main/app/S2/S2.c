#include "S2.h"
#include "service/screen/screen.h"
#include "esp_log.h"

static const char *TAG = "S2";

/* ===== S2 已登录未开始测量 ===== */
void S2_enter(void)
{
    ESP_LOGI(TAG, "进入 S2 已登录未开始");
    screen_show_status("READY");
}
