#ifndef SERVICE_MOBILE_H
#define SERVICE_MOBILE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "bsp/err.h"

typedef enum { SERVICE_MOBILE_IDLE, SERVICE_MOBILE_MEASURING, SERVICE_MOBILE_PAUSED } service_mobile_state_t;
typedef enum { SERVICE_MOBILE_STORAGE_UNKNOWN, SERVICE_MOBILE_STORAGE_ABSENT,
    SERVICE_MOBILE_STORAGE_READY, SERVICE_MOBILE_STORAGE_ERROR } service_mobile_storage_t;
typedef enum { SERVICE_MOBILE_START, SERVICE_MOBILE_PAUSE, SERVICE_MOBILE_RESET,
    SERVICE_MOBILE_TIME_SYNC } service_mobile_command_t;
typedef enum { SERVICE_MOBILE_OK, SERVICE_MOBILE_BUSY, SERVICE_MOBILE_EXPIRED,
    SERVICE_MOBILE_INVALID_ARGUMENT, SERVICE_MOBILE_NOT_READY,
    SERVICE_MOBILE_INTERNAL_ERROR } service_mobile_result_t;

typedef struct {
    uint64_t token;
    service_mobile_command_t command;
    uint8_t hour, minute, second;
    uint64_t expires_uptime_ms;
} service_mobile_request_t;

typedef struct {
    uint64_t seq, state_seq, sample_uptime_ms;
    double total_ml, rate_mlh;
    uint64_t total_drops, elapsed_s;
    service_mobile_state_t state;
    char alert;
    int battery_percent; /* -1 表示未知，否则为 0..100。 */
    service_mobile_storage_t storage_state;
    bool storage_checked_known;
    uint64_t storage_checked_uptime_ms;
    char clock_hms[9];
    bool time_synced;
} service_mobile_snapshot_t;

typedef struct {
    const char *device_id; /* ASCII 标识，启动时拷贝。 */
    const char *ap_ssid, *ap_password;
    uint8_t ap_channel, ap_max_connections;
    err_t (*submit)(const service_mobile_request_t *request, void *ctx);
    bool (*snapshot)(service_mobile_snapshot_t *out, void *ctx);
    uint64_t (*monotonic_ms)(void *ctx);
    void *ctx;
} service_mobile_config_t;

/* start/stop 必须由唯一一个生命周期拥有者串行调用；stop 必须在驱动回调
 * 之外执行。tick 只有一个拥有者。snapshot/submit/send 一律在服务互斥锁之外。
 * submit 在返回 ERR_OK 之前已拷贝请求；被接受的请求在提交五秒后过期，
 * 由拥有者在执行前检查。complete 可与回调/tick 并发运行，甚至可在 submit
 * 返回之前发生。令牌标识某个连接代际中的某一次请求。 */
err_t service_mobile_start(const service_mobile_config_t *config);
void service_mobile_stop(void);
void service_mobile_tick(void);
/* 仅暂停状态快照推送，保留热点、会话、握手及命令确认。
 * 由唯一存储属主配对调用；函数返回后不再排入新的状态帧，
 * 已排队的帧和底层网络流量可继续完成。恢复后允许立即推送。 */
void service_mobile_set_push_paused(bool paused);
void service_mobile_complete(uint64_t token, service_mobile_result_t result,
                             service_mobile_state_t state, uint64_t state_seq);
#endif
