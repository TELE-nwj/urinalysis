#include "driver/wifi/wifi.h"
#include "driver/mobile_link/mobile_link.h"
#include "esp_log.h"

static const char *TAG = "mobile_ap_smoke";
static const char HELLO[] =
    "{\"type\":\"hello\",\"mode\":\"SIMULATED\",\"protocol\":\"mobile-ap-smoke-v1\"}";

static void on_open(mobile_link_peer_t peer, void *ctx)
{
    (void)ctx;
    err_t result = mobile_link_send_text(peer, HELLO, sizeof(HELLO) - 1);
    ESP_LOGI(TAG, "SIMULATED open peer=%lu hello_queue=%ld",
             (unsigned long)peer, (long)result);
}

static void on_text(mobile_link_peer_t peer, const char *text, size_t len, void *ctx)
{
    (void)ctx;
    err_t result = mobile_link_send_text(peer, text, len);
    ESP_LOGI(TAG, "SIMULATED raw echo peer=%lu bytes=%u queue=%ld",
             (unsigned long)peer, (unsigned)len, (long)result);
}

static void on_close(mobile_link_peer_t peer, void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "SIMULATED close peer=%lu", (unsigned long)peer);
}

void app_main(void)
{
    /* Trial-only credentials and trial-only business messages. This main never
     * initializes sensors, CH376, serial screen, buttons, measurement or USB. */
    const wifi_ap_config_t ap = {
        .ssid = "NIAIO-F0-SIMULATED",
        .password = "niaio-f0-test",
        .channel = 6,
        .max_connections = 4,
    };
    err_t result = wifi_ap_start(&ap);
    if (result != ERR_OK) {
        ESP_LOGE(TAG, "SoftAP failed: %ld; existing NVS is preserved", (long)result);
        return;
    }
    const mobile_link_config_t link = {
        .port = 80,
        .ws_path = "/ws",
        .max_frame_size = 1024,
    };
    const mobile_link_callbacks_t callbacks = {
        .on_open = on_open,
        .on_text = on_text,
        .on_close = on_close,
    };
    result = mobile_link_start(&link, &callbacks);
    if (result != ERR_OK) {
        ESP_LOGE(TAG, "WebSocket failed: %ld", (long)result);
        wifi_ap_stop();
        return;
    }
    ESP_LOGI(TAG, "SIMULATED: connect WiFi NIAIO-F0-SIMULATED; ws://192.168.4.1/ws");
}
