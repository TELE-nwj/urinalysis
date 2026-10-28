#ifndef DRIVER_MOBILE_LINK_H
#define DRIVER_MOBILE_LINK_H

#include <stddef.h>
#include <stdint.h>
#include "bsp/err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque generation token, never a socket descriptor. Zero is invalid. */
typedef uint32_t mobile_link_peer_t;

typedef struct {
    void (*on_open)(mobile_link_peer_t peer, void *ctx);
    void (*on_text)(mobile_link_peer_t peer, const char *text, size_t len, void *ctx);
    void (*on_close)(mobile_link_peer_t peer, void *ctx);
    void *ctx;
} mobile_link_callbacks_t;

typedef struct {
    uint16_t port;
    const char *ws_path;
    size_t max_frame_size;
} mobile_link_config_t;

/* Requires CONFIG_HTTPD_WS_SUPPORT, CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT
 * and CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT.
 * All fields are required; max_frame_size must be in [1,1024]. Configuration
 * and callback structures are copied. The caller owns callback ctx until stop
 * returns. Text pointer is valid only during on_text; len is authoritative.
 * Callbacks run on the HTTPD task outside the driver's lock, and may call send.
 * Start/stop must be serialized by one external lifecycle owner task. Concurrent
 * lifecycle calls are outside this contract (start returns ERR_BUSY and stop
 * ignores transitions). Stop invoked from a callback is an explicit no-op to
 * prevent HTTPD self-deadlock. All other callbacks may safely call send.
 */
err_t mobile_link_start(const mobile_link_config_t *config,
                        const mobile_link_callbacks_t *callbacks);

/* Copies payload and queues delivery. ERR_OK means queued, not delivered.
 * At most four outstanding frames; ERR_BUSY applies backpressure. A queued
 * frame is discarded if its generation/session has closed before execution.
 */
err_t mobile_link_send_text(mobile_link_peer_t peer, const char *text, size_t len);

/* On return from the serialized external lifecycle owner, no callbacks remain
 * in flight and queued payloads have been reclaimed. No-op if already stopped.
 * SDK shutdown failures are retried while retaining all callback state; a
 * permanent SDK failure will block stop (or start's registration rollback).
 */
void mobile_link_stop(void);

#ifdef __cplusplus
}
#endif
#endif
