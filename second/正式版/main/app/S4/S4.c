#include "S4.h"
#include "service/screen/screen.h"
#include "esp_log.h"

static const char *TAG = "S4";

/* ===== S4 测量暂停 =====
 * 数据已冻结在屏上(S3_pause 里推的)，这里只把状态刷成 PAUSE。 */
void S4_enter(void)
{
    ESP_LOGI(TAG, "进入 S4 测量暂停");
    screen_show_status("PAUSE");
}
