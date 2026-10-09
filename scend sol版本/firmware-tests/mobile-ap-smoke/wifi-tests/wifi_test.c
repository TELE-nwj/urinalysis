#include "driver/wifi/wifi.h"
#include "idf_stubs.h"

/* Freestanding RV32 executable, using the existing IDF toolchain. */
void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = dst; const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}
void *memset(void *dst, int value, size_t n) {
    unsigned char *d = dst; while (n--) *d++ = (unsigned char)value;
    return dst;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
size_t strnlen(const char *s, size_t max) { size_t n = 0; while (n < max && s[n]) n++; return n; }

static int fail_at, calls, erase_calls, loop_delete_calls;
static int netifs, init_count, deinit_count, stop_count, ram_storage;
static int existing_loop, existing_ap, existing_wifi, nvs_error;
static int fail_stop, fail_deinit;
static wifi_config_t applied_config;
static esp_netif_t netif;
static esp_err_t next(void) { return ++calls == fail_at ? ESP_FAIL : ESP_OK; }
static void reset(void) {
    wifi_ap_stop();
    fail_at = calls = erase_calls = loop_delete_calls = 0;
    netifs = init_count = deinit_count = stop_count = ram_storage = 0;
    existing_loop = existing_ap = existing_wifi = nvs_error = 0;
    fail_stop = fail_deinit = 0;
    memset(&applied_config, 0, sizeof(applied_config));
}
esp_err_t nvs_flash_init(void) { return nvs_error ? nvs_error : next(); }
esp_err_t nvs_flash_erase(void) { erase_calls++; return ESP_OK; }
esp_err_t esp_netif_init(void) { return next(); }
esp_err_t esp_event_loop_create_default(void) { return existing_loop ? ESP_ERR_INVALID_STATE : next(); }
esp_err_t esp_event_loop_delete_default(void) { loop_delete_calls++; return ESP_OK; }
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key) { (void)key; return existing_ap ? &netif : 0; }
esp_netif_t *esp_netif_new(const esp_netif_config_t *config) {
    (void)config; if (next() != ESP_OK) return 0; netifs++; return &netif;
}
void esp_netif_destroy_default_wifi(void *value) { if (value) netifs--; }
esp_err_t esp_netif_attach_wifi_ap(esp_netif_t *value) { (void)value; return next(); }
esp_err_t esp_wifi_set_default_wifi_ap_handlers(void) { return next(); }
esp_err_t esp_wifi_init(const wifi_init_config_t *config) {
    (void)config; esp_err_t rc = next(); if (rc == ESP_OK) init_count++; return rc;
}
esp_err_t esp_wifi_get_mode(wifi_mode_t *mode) { *mode = WIFI_MODE_AP; return existing_wifi ? ESP_OK : ESP_ERR_WIFI_NOT_INIT; }
esp_err_t esp_wifi_deinit(void) { if (fail_deinit) return ESP_FAIL; deinit_count++; return ESP_OK; }
esp_err_t esp_wifi_set_storage(int storage) { ram_storage = storage == WIFI_STORAGE_RAM; return next(); }
esp_err_t esp_wifi_set_mode(int mode) { (void)mode; return next(); }
esp_err_t esp_wifi_set_config(int interface, wifi_config_t *config) {
    (void)interface; applied_config = *config; return next();
}
esp_err_t esp_wifi_start(void) { return next(); }
esp_err_t esp_wifi_stop(void) { if (fail_stop) return ESP_FAIL; stop_count++; return ESP_OK; }
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *buffer) { return buffer; }
int xSemaphoreTake(SemaphoreHandle_t handle, unsigned timeout) { (void)handle; (void)timeout; return 1; }
int xSemaphoreGive(SemaphoreHandle_t handle) { (void)handle; return 1; }

#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)
#define TEST(name) int name(void)
static wifi_ap_config_t valid = {"NIAIO-F0-SIMULATED", "niaio-f0-test", 6, 1};

TEST(test_invalid_configuration_has_no_side_effects) {
    reset();
    CHECK(wifi_ap_start(0) == ERR_PARAM);
    wifi_ap_config_t config = valid;
    config.ssid = ""; CHECK(wifi_ap_start(&config) == ERR_PARAM);
    config = valid; config.password = "short"; CHECK(wifi_ap_start(&config) == ERR_PARAM);
    config = valid; config.channel = 0; CHECK(wifi_ap_start(&config) == ERR_PARAM);
    config = valid; config.max_connections = 11; CHECK(wifi_ap_start(&config) == ERR_PARAM);
    CHECK(calls == 0 && !wifi_ap_is_running()); return 0;
}
TEST(test_nvs_failure_never_erases_user_data) {
    reset(); nvs_error = ESP_ERR_NVS_NO_FREE_PAGES;
    CHECK(wifi_ap_start(&valid) == ERR_FAIL);
    CHECK(erase_calls == 0 && calls == 0 && !wifi_ap_is_running());
    nvs_error = ESP_ERR_NVS_NEW_VERSION_FOUND;
    CHECK(wifi_ap_start(&valid) == ERR_FAIL);
    CHECK(erase_calls == 0 && calls == 0); return 0;
}
TEST(test_existing_global_loop_is_preserved) {
    reset(); existing_loop = 1;
    CHECK(wifi_ap_start(&valid) == ERR_OK && wifi_ap_is_running());
    wifi_ap_stop(); CHECK(!wifi_ap_is_running());
    CHECK(loop_delete_calls == 0 && netifs == 0 && init_count == deinit_count);
    return 0;
}
TEST(test_ram_credentials_and_lifecycle) {
    reset(); CHECK(wifi_ap_start(&valid) == ERR_OK);
    CHECK(wifi_ap_is_running() && ram_storage);
    CHECK(applied_config.ap.ssid_len == 18 && applied_config.ap.channel == 6);
    CHECK(applied_config.ap.max_connection == 1 && applied_config.ap.authmode == WIFI_AUTH_WPA2_PSK);
    int before = calls; CHECK(wifi_ap_start(&valid) == ERR_BUSY && calls == before);
    wifi_ap_stop(); CHECK(!wifi_ap_is_running() && netifs == 0);
    CHECK(init_count == 1 && deinit_count == 1 && stop_count == 1);
    wifi_ap_stop(); CHECK(deinit_count == 1 && stop_count == 1);
    CHECK(wifi_ap_start(&valid) == ERR_OK); wifi_ap_stop();
    CHECK(init_count == 2 && deinit_count == 2 && netifs == 0); return 0;
}
TEST(test_each_sdk_failure_rolls_back_owned_resources) {
    reset(); CHECK(wifi_ap_start(&valid) == ERR_OK);
    int total = calls; wifi_ap_stop();
    for (int stage = 1; stage <= total; stage++) {
        reset(); fail_at = stage;
        CHECK(wifi_ap_start(&valid) != ERR_OK);
        CHECK(!wifi_ap_is_running() && netifs == 0);
        CHECK(init_count == deinit_count && erase_calls == 0 && loop_delete_calls == 0);
    }
    reset(); CHECK(wifi_ap_start(&valid) == ERR_OK); wifi_ap_stop(); return 0;
}
TEST(test_existing_ap_netif_rejected) {
    reset(); existing_ap = 1;
    CHECK(wifi_ap_start(&valid) == ERR_BUSY);
    CHECK(!wifi_ap_is_running() && init_count == 0 && netifs == 0); return 0;
}
TEST(test_existing_wifi_driver_is_not_taken_over) {
    reset(); existing_wifi = 1;
    CHECK(wifi_ap_start(&valid) == ERR_BUSY);
    CHECK(init_count == 0 && deinit_count == 0 && netifs == 0 && !wifi_ap_is_running());
    return 0;
}
TEST(test_open_ap_and_full_length_ssid) {
    reset(); wifi_ap_config_t config = {"12345678901234567890123456789012", 0, 1, 10};
    CHECK(wifi_ap_start(&config) == ERR_OK);
    CHECK(applied_config.ap.authmode == WIFI_AUTH_OPEN && applied_config.ap.ssid_len == 32);
    wifi_ap_stop(); return 0;
}
TEST(test_stop_failure_keeps_running_state_and_can_retry) {
    reset(); CHECK(wifi_ap_start(&valid) == ERR_OK);
    fail_stop = 1; wifi_ap_stop();
    CHECK(wifi_ap_is_running() && netifs == 1 && deinit_count == 0);
    CHECK(wifi_ap_start(&valid) == ERR_BUSY);
    fail_stop = 0; wifi_ap_stop();
    CHECK(!wifi_ap_is_running() && netifs == 0 && deinit_count == 1);
    return 0;
}
TEST(test_deinit_failure_keeps_owned_resources_for_retry) {
    reset(); CHECK(wifi_ap_start(&valid) == ERR_OK);
    fail_deinit = 1; wifi_ap_stop();
    CHECK(!wifi_ap_is_running() && netifs == 1 && deinit_count == 0);
    CHECK(wifi_ap_start(&valid) == ERR_BUSY);
    fail_deinit = 0; wifi_ap_stop();
    CHECK(netifs == 0 && deinit_count == 1); return 0;
}
