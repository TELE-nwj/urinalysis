#include "storage.h"
#include "driver/ch376/ch376_spi.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "storage";
static bool s_ready = false;

err_t storage_init(void)
{
    CH376_PORT_INIT();              /* 1) GPIO 软 SPI 引脚 */

    /* 2) 最前方先“取芯片版本”——CMD_GET_IC_VER 不依赖 USB 中断，
          只要 SPI 通、芯片在位就能立即读出，用于先确认链路/芯片自检。
          (常见 CH376 版本号参考值, 若读到 0x00/0xFF 多为接线或供电问题) */
    uint8_t ver = CH376T_Get_IC_Ver();
    ESP_LOGI(TAG, "CH376 版本号: 0x%02X", ver);

    if (!CH376T_SetUsbMode()) {          /* 3) 进 USB 主机模式 */
        ESP_LOGE(TAG, "USB主机模式失败");
        return ERR_FAIL;
    }
    if (!CH376T_CMD_DISK_CONNECT()) {    /* 4) 检测磁盘(需插入U盘) */
        ESP_LOGE(TAG, "磁盘未连接");
        return ERR_FAIL;
    }
    s_ready = true;
    ESP_LOGI(TAG, "storage ready (USB disk connected)");
    return ERR_OK;
}

err_t storage_write_text(const char *text)
{
    if (!s_ready) return ERR_NOT_READY;
    if (!text) return ERR_PARAM;

    /* TODO: 目前为"创建(覆盖)后写入"；如需跨次追加，改为先定位文件末尾再写。 */
    if (!CH376T_FILE_CREATE()) {         /* 创建/打开 /ASD.TXT */
        ESP_LOGE(TAG, "创建文件失败");
        return ERR_FAIL;
    }
    uint16_t len = (uint16_t)strlen(text);
    if (!CH376T_BYTE_WRITE_DATA((const uint8_t *)text, len)) {
        ESP_LOGE(TAG, "写入失败");
        return ERR_FAIL;
    }
    if (!CH376T_FILE_CLOSE()) {
        ESP_LOGE(TAG, "关闭文件失败");
        return ERR_FAIL;
    }
    return ERR_OK;
}
