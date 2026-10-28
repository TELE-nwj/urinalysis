idf_component_register(
    SRCS
        "main.c"
        "app/monitor/monitor.c"
        "app/S1/S1.c"
        "app/S2/S2.c"
        "app/S3/S3.c"
        "app/S4/S4.c"
        "service/urine/urine.c"
        "service/screen/screen.c"
        "service/storage/storage.c"
        "driver/time_svc/time_svc.c"
        "driver/ch376/ch376_spi.c"
        "driver/drop/drop.c"
        "driver/screen_uart/screen_uart.c"
        "driver/key/key.c"
        # 本板(正式版)不用 ADC 电池检测，下面驱动保留在仓库但暂不编入：
        #   driver/battery/battery.c 电池 ADC(原 GPIO7) —— 改由 INA228 走 I2C
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_common
        esp_timer
        esp_driver_uart
        esp_driver_gpio
        log
        freertos
)
