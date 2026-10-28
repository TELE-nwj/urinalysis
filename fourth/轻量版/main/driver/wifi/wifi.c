#include "wifi.h"
#include <string.h>
#include "esp_event.h"
#include "esp_netif.h"
/* 冻结的应用类型与 ESP-IDF 中基于数组的
 * AP 配置同名。把这个仅属于 SDK 的 typedef 隔离在本实现文件内。 */
#define wifi_ap_config_t wifi_idf_ap_config_t
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#undef wifi_ap_config_t
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static StaticSemaphore_t s_mutex_buffer;
static SemaphoreHandle_t s_mutex;
static portMUX_TYPE s_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;
static esp_netif_t *s_ap_netif;
static bool s_wifi_initialized;
static bool s_running;

static void lock(void)
{
    portENTER_CRITICAL(&s_mutex_init_lock);
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_buffer);
    }
    portEXIT_CRITICAL(&s_mutex_init_lock);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
}

static err_t translate_error(esp_err_t error)
{
    if (error == ESP_OK) return ERR_OK;
    if (error == ESP_ERR_NO_MEM) return ERR_NO_MEM;
    if (error == ESP_ERR_INVALID_ARG) return ERR_PARAM;
    if (error == ESP_ERR_INVALID_STATE) return ERR_BUSY;
    return ERR_FAIL;
}

/* 调用时已持有生命周期互斥锁。绝不反初始化共享的 NVS、网络协议栈或
 * 默认事件循环：它们可能被其它组件使用。 */
static void release_owned_resources(void)
{
    if (s_wifi_initialized) {
        if (s_running) {
            if (esp_wifi_stop() != ESP_OK) return;
            s_running = false;
        }
        if (esp_wifi_deinit() != ESP_OK) return;
        s_wifi_initialized = false;
    }
    s_running = false;
    if (s_ap_netif) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
    }
}

static bool valid_config(const wifi_ap_config_t *config)
{
    if (!config || !config->ssid) return false;
    size_t ssid_len = strnlen(config->ssid, 33);
    size_t password_len = config->password ? strnlen(config->password, 64) : 0;
    return ssid_len >= 1 && ssid_len <= 32 &&
           (password_len == 0 || (password_len >= 8 && password_len <= 63)) &&
           config->channel >= 1 && config->channel <= 13 &&
           config->max_connections >= 1 && config->max_connections <= 10;
}

err_t wifi_ap_start(const wifi_ap_config_t *config)
{
    if (!valid_config(config)) return ERR_PARAM;
    lock();
    if (s_running || s_wifi_initialized || s_ap_netif) {
        xSemaphoreGive(s_mutex);
        return ERR_BUSY;
    }
    /* esp_wifi_init() 在 IDF 中是幂等的，因此这里显式拒绝他人已经初始化好的
     * 射频，而不是等到之后去反初始化他们的驱动。 */
    wifi_mode_t existing_mode;
    esp_err_t result = esp_wifi_get_mode(&existing_mode);
    if (result == ESP_OK) {
        result = ESP_ERR_INVALID_STATE;
        goto failed;
    }
    if (result != ESP_ERR_WIFI_NOT_INIT) goto failed;
    result = nvs_flash_init();
    /* 特别地，NO_FREE_PAGES 与 NEW_VERSION_FOUND 会原样返回给
     * 调用者。擦除既有用户的分区绝不是一个恢复步骤。 */
    if (result != ESP_OK) goto failed;
    result = esp_netif_init(); /* IDF 5.5.5 在已初始化时同样返回 OK。 */
    if (result != ESP_OK) goto failed;
    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) goto failed;
    if (esp_netif_get_handle_from_ifkey("WIFI_AP_DEF")) {
        result = ESP_ERR_INVALID_STATE;
        goto failed;
    }
    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_WIFI_AP();
    /* 使用显式的 API 调用序列，使内存/挂载失败时返回错误，
     * 而不是触发 esp_netif_create_default_wifi_ap() 中的 assert。 */
    s_ap_netif = esp_netif_new(&netif_config);
    if (!s_ap_netif) {
        result = ESP_ERR_NO_MEM;
        goto failed;
    }
    result = esp_netif_attach_wifi_ap(s_ap_netif);
    if (result != ESP_OK) goto failed;
    result = esp_wifi_set_default_wifi_ap_handlers();
    if (result != ESP_OK) goto failed;
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&init_config);
    if (result != ESP_OK) goto failed;
    s_wifi_initialized = true;
    result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result != ESP_OK) goto failed;
    result = esp_wifi_set_mode(WIFI_MODE_AP);
    if (result != ESP_OK) goto failed;
    wifi_config_t wifi_config = {0};
    size_t ssid_len = strlen(config->ssid);
    size_t password_len = config->password ? strlen(config->password) : 0;
    memcpy(wifi_config.ap.ssid, config->ssid, ssid_len);
    if (password_len) memcpy(wifi_config.ap.password, config->password, password_len);
    wifi_config.ap.ssid_len = (uint8_t)ssid_len;
    wifi_config.ap.channel = config->channel;
    wifi_config.ap.max_connection = config->max_connections;
    wifi_config.ap.authmode = password_len ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    result = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (result != ESP_OK) goto failed;
    result = esp_wifi_start();
    if (result != ESP_OK) goto failed;
    s_running = true;
    xSemaphoreGive(s_mutex);
    return ERR_OK;

failed:
    release_owned_resources();
    xSemaphoreGive(s_mutex);
    return translate_error(result);
}

void wifi_ap_stop(void)
{
    lock();
    release_owned_resources();
    xSemaphoreGive(s_mutex);
}

bool wifi_ap_is_running(void)
{
    lock();
    bool running = s_running;
    xSemaphoreGive(s_mutex);
    return running;
}
