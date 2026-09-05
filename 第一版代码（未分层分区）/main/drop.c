
#include "dorp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

// static const char *TAG = "INTR_10";


#define IT_GPIO_PIN     GPIO_NUM_10   // 中断引脚

#define GET_TIME_SECONDS() ((double)esp_timer_get_time() / 1000000.0)



uint8_t Time_Difference (uint64_t current_time, uint64_t last_interrupt_time)
{
    uint64_t diff_us = current_time - last_interrupt_time;
    uint32_t diff_sec_x10 = (uint32_t)(diff_us / 1000000ULL);  // 转为 0.1秒为单位
    
    if (diff_sec_x10 > 255) {
        return 255;  // 饱和处理（实际最大5秒=50，远小于255）
    }
    return (uint8_t)diff_sec_x10;
}


// 中断服务函数（带消抖）
void IRAM_ATTR IT_Function(void *arg)
{
    current_time = esp_timer_get_time();
    
    // 消抖：20ms内只响应一次
    if (current_time - last_interrupt_time < 500000) {
        return;  // 抖动，忽略
    }
    
    last_interrupt_time = current_time;
    interrupt_flag = true;
    interrupt_count = interrupt_count + 1;

}

// 中断初始化
void Z_Init_IT_GPIO(void)
{
    gpio_config_t init = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << IT_GPIO_PIN),
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    gpio_config(&init);

    gpio_install_isr_service(ESP_INTR_FLAG_EDGE);
    gpio_isr_handler_add(IT_GPIO_PIN, IT_Function, NULL);
}



