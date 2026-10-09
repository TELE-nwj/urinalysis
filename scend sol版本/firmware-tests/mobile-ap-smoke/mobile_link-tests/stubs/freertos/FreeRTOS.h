#pragma once
#include <stdint.h>
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
void test_lock(portMUX_TYPE *);
void test_unlock(portMUX_TYPE *);
#define portENTER_CRITICAL(m) test_lock(m)
#define portEXIT_CRITICAL(m) test_unlock(m)
#define pdMS_TO_TICKS(ms) (ms)
