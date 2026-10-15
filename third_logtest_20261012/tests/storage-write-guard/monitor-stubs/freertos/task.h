#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
BaseType_t xTaskCreate(void (*)(void *),const char *,unsigned,void *,unsigned,TaskHandle_t *);
void vTaskDelay(unsigned);
void vTaskDelete(TaskHandle_t);
