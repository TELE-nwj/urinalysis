#include "S1.h"
#include "service/screen/screen.h"
#include "esp_log.h"

static const char *TAG = "S1";

/* ===== S1 复位待登录 =====
 * 未登录、无当前用户。
 * (登录时按用户名建/开 U盘文件 storage_open_user() 已临时注释) */
void S1_enter(void)
{
    ESP_LOGI(TAG, "进入 S1 复位待登录");
    screen_show_status("LOGIN");
}
