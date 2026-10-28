#ifndef DRIVER_WIFI_H
#define DRIVER_WIFI_H

#include <stdbool.h>
#include <stdint.h>
#include "bsp/err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *ssid;
    const char *password;
    uint8_t channel;
    uint8_t max_connections;
} wifi_ap_config_t;

/* 任务上下文调用。启动后即独占 WiFi 射频直到 stop；若已存在 WiFi 驱动
 * 或默认 AP netif，则拒绝启动。SSID：1..32 字节；password：NULL/空（开放），或 8..63
 * 字节（WPA2）；channel：1..13；max_connections：1..10。
 * 凭据使用 RAM 存储。NVS 初始化失败绝不擦除用户数据。
 * 共享的默认事件循环与 esp-netif 协议栈保持已初始化状态。
 * 若 SDK stop/deinit 失败，则保留所有权以便之后重试 stop；
 * 若 SDK stop 失败则 is_running 保持 true，且 start 仍返回 ERR_BUSY。 */
err_t wifi_ap_start(const wifi_ap_config_t *config);
void wifi_ap_stop(void);
bool wifi_ap_is_running(void);

#ifdef __cplusplus
}
#endif

#endif
