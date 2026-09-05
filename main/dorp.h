#ifndef DORP
#define DORP

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif
// 声明外部变量
extern volatile bool interrupt_flag;
extern volatile uint32_t interrupt_count;
extern volatile uint64_t last_interrupt_time;
extern volatile uint64_t current_time;
extern volatile uint8_t Time[255];
// 中断回调函数类型
typedef void (*gpio_interrupt_callback_t)(void);

void Time_Calculation(void);
void IT_Function(void *arg);
void Z_Init_IT_GPIO(void);
uint8_t Time_Difference (uint64_t current_time, uint64_t last_interrupt_time);
#ifdef __cplusplus
}
#endif

#endif