set(SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../second sol/main")
idf_component_register(
    SRCS "main.c"
        "${SOURCE_ROOT}/app/monitor/monitor.c"
        "${SOURCE_ROOT}/app/S1/S1.c" "${SOURCE_ROOT}/app/S2/S2.c"
        "${SOURCE_ROOT}/app/mobile/mobile.c"
        "${SOURCE_ROOT}/service/mobile/mobile.c"
        "${SOURCE_ROOT}/service/urine/urine.c"
        "${SOURCE_ROOT}/service/screen/screen.c"
        "${SOURCE_ROOT}/service/storage/storage.c"
        "${SOURCE_ROOT}/service/power/power.c"
        "${SOURCE_ROOT}/service/clock/clock.c"
        "${SOURCE_ROOT}/service/input/input.c"
        "${SOURCE_ROOT}/driver/time_svc/time_svc.c"
        "${SOURCE_ROOT}/driver/walltime/walltime.c"
        "${SOURCE_ROOT}/driver/ch376/ch376_spi.c"
        "${SOURCE_ROOT}/driver/drop/drop.c"
        "${SOURCE_ROOT}/driver/screen_uart/screen_uart.c"
        "${SOURCE_ROOT}/driver/key/key.c"
        "${SOURCE_ROOT}/driver/battery/battery.c"
        "${SOURCE_ROOT}/driver/wifi/wifi.c"
        "${SOURCE_ROOT}/driver/mobile_link/mobile_link.c"
    INCLUDE_DIRS "${SOURCE_ROOT}"
    REQUIRES esp_common esp_timer esp_driver_uart esp_driver_gpio esp_adc
        log freertos esp_wifi nvs_flash esp_netif esp_event esp_http_server json
)
target_compile_options(${COMPONENT_LIB} PRIVATE -fstack-usage)
