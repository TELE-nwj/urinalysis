idf_component_register(
    SRCS
        "main.c"
        # ---- app 层：三态状态机（待开始 / 测量中=S1 / 暂停=S2）----
        "app/monitor/monitor.c"
        "app/S1/S1.c"
        "app/S2/S2.c"
        # ---- service 层：app 唯一可见的接口面 ----
        "service/urine/urine.c"
        "service/screen/screen.c"
        "service/storage/storage.c"
        "service/power/power.c"
        "service/clock/clock.c"
        "service/input/input.c"
        # ---- driver 层：硬件 ----
        "driver/time_svc/time_svc.c"
        "driver/walltime/walltime.c"
        "driver/ch376/ch376_spi.c"
        "driver/ch376/ch376_diag.c"
        "driver/drop/drop.c"
        "driver/screen_uart/screen_uart.c"
        "driver/key/key.c"
        "driver/battery/battery.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_common
        esp_timer
        esp_driver_uart
        esp_driver_gpio
        esp_adc          # 电池分压采样(adc_oneshot/adc_cali)
        log
        freertos
)
target_compile_options(${COMPONENT_LIB} PRIVATE -fstack-usage)
