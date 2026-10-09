set(DRIVER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../second sol/main")
idf_component_register(
    SRCS
        "main.c"
        "${DRIVER_ROOT}/driver/wifi/wifi.c"
        "${DRIVER_ROOT}/driver/mobile_link/mobile_link.c"
    INCLUDE_DIRS "${DRIVER_ROOT}"
    REQUIRES esp_wifi nvs_flash esp_netif esp_event esp_http_server freertos log
)
