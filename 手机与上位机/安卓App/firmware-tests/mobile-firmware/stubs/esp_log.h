#pragma once
void test_log(const char *, const char *, ...);
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGE(...) test_log(__VA_ARGS__)
typedef int esp_log_level_t;
#define ESP_LOG_INFO 3
#define ESP_LOG_NONE 0
int esp_log_level_get(const char *);
void esp_log_level_set(const char *, int);
