#pragma once
#include <stdint.h>
typedef int portMUX_TYPE;
typedef int BaseType_t;
#define portMUX_INITIALIZER_UNLOCKED 0
#define pdPASS 1
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(ms) (ms)
void test_lock(portMUX_TYPE *);
void test_unlock(portMUX_TYPE *);
#define portENTER_CRITICAL(m) test_lock(m)
#define portEXIT_CRITICAL(m) test_unlock(m)
