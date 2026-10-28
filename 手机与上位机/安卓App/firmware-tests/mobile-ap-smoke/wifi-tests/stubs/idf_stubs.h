#ifndef IDF_STUBS_H
#define IDF_STUBS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NVS_NO_FREE_PAGES 0x110d
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x1110
#define ESP_ERR_WIFI_NOT_INIT 0x3001
typedef struct { int unused; } esp_netif_t;
typedef struct { int unused; } esp_netif_config_t;
#define ESP_NETIF_DEFAULT_WIFI_AP() ((esp_netif_config_t){0})
esp_err_t esp_netif_init(void);
esp_netif_t *esp_netif_new(const esp_netif_config_t *config);
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key);
void esp_netif_destroy_default_wifi(void *netif);
esp_err_t esp_netif_attach_wifi_ap(esp_netif_t *netif);
esp_err_t esp_wifi_set_default_wifi_ap_handlers(void);
esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_loop_delete_default(void);
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);
typedef struct { int unused; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
#define WIFI_STORAGE_RAM 1
#define WIFI_MODE_AP 2
typedef int wifi_mode_t;
#define WIFI_IF_AP 1
#define WIFI_AUTH_OPEN 0
#define WIFI_AUTH_WPA2_PSK 3
typedef struct {
    uint8_t ssid[32];
    uint8_t password[64];
    uint8_t ssid_len, channel, max_connection;
    int authmode;
} wifi_idf_ap_config_t;
typedef union { wifi_idf_ap_config_t ap; } wifi_config_t;
esp_err_t esp_wifi_init(const wifi_init_config_t *config);
esp_err_t esp_wifi_get_mode(wifi_mode_t *mode);
esp_err_t esp_wifi_deinit(void);
esp_err_t esp_wifi_set_storage(int storage);
esp_err_t esp_wifi_set_mode(int mode);
esp_err_t esp_wifi_set_config(int interface, wifi_config_t *config);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
typedef struct { int unused; } StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portMAX_DELAY 0xffffffffu
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *buffer);
int xSemaphoreTake(SemaphoreHandle_t semaphore, unsigned timeout);
int xSemaphoreGive(SemaphoreHandle_t semaphore);
#endif
