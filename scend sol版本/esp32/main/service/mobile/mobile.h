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
    int battery_percent; /* -1 means unknown, otherwise 0..100. */
    service_mobile_storage_t storage_state;
    bool storage_checked_known;
    uint64_t storage_checked_uptime_ms;
    char clock_hms[9];
    bool time_synced;
} service_mobile_snapshot_t;

typedef struct {
    const char *device_id; /* ASCII ID, copied at start. */
    const char *ap_ssid, *ap_password;
    uint8_t ap_channel, ap_max_connections;
    err_t (*submit)(const service_mobile_request_t *request, void *ctx);
    bool (*snapshot)(service_mobile_snapshot_t *out, void *ctx);
    uint64_t (*monotonic_ms)(void *ctx);
    void *ctx;
} service_mobile_config_t;

/* One lifecycle owner must serialize start/stop; stop must be outside driver
 * callbacks. tick has one owner. Snapshot/submit/send are always outside the
 * service mutex. submit copies the request before returning ERR_OK; an accepted
 * request expires five seconds after submission, checked before execution by
 * its owner. complete can run concurrently with callbacks/tick, even before
 * submit returns. Tokens identify one request in one connection generation. */
err_t service_mobile_start(const service_mobile_config_t *config);
void service_mobile_stop(void);
void service_mobile_tick(void);
void service_mobile_complete(uint64_t token, service_mobile_result_t result,
                             service_mobile_state_t state, uint64_t state_seq);
#endif
