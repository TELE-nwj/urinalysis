/* BSP and lwIP both own the err_t/ERR_OK names with different types. Load
 * lwIP's private socket boundary before BSP macros, renaming only its typedef.
 * The public driver API below retains BSP's int32_t err_t unchanged. */
#define err_t mobile_link_lwip_err_t
#include "lwip/sockets.h"
#undef err_t

#include "mobile_link.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if !CONFIG_HTTPD_WS_SUPPORT || !CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT \
    || !CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT
#error "mobile_link requires HTTPD WebSocket, pre/post-handshake callback support"
#endif

#define FRAME_LIMIT 1024U
#define QUEUE_LIMIT 4U

typedef enum { STOPPED, STARTING, RUNNING, STOPPING } link_phase_t;

typedef struct send_job {
    struct send_job *next;
    httpd_handle_t server;
    mobile_link_peer_t peer;
    int fd;
    size_t len;
    char text[];
} send_job_t;

/* Critical sections only protect bounded state updates. HTTPD calls, heap
 * operations, waits and user callbacks always execute outside this lock. */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static link_phase_t s_phase;
static httpd_handle_t s_server;
static TaskHandle_t s_httpd_task;
static mobile_link_callbacks_t s_callbacks;
static char *s_path;
static size_t s_max_frame;
static mobile_link_peer_t s_generation;
static mobile_link_peer_t s_peer;
static int s_fd = -1;
static bool s_opened;
static bool s_closing;
static send_job_t *s_jobs;
static unsigned s_job_count;
static unsigned s_submitters;

static void remove_job_locked(send_job_t *job)
{
    send_job_t **entry = &s_jobs;
    while (*entry && *entry != job) entry = &(*entry)->next;
    if (*entry) {
        *entry = job->next;
        --s_job_count;
    }
}

static void send_work(void *arg)
{
    send_job_t *job = arg;
    portENTER_CRITICAL(&s_lock);
    bool valid = s_phase == RUNNING && s_opened && !s_closing && s_server == job->server
                 && s_peer == job->peer && s_fd == job->fd;
    portEXIT_CRITICAL(&s_lock);

    /* This executes on HTTPD's own task, which also owns socket close/upgrade.
     * Thus fd state cannot be reused between this check and the actual send.
     * A concurrent external stop waits for HTTPD to exit before freeing jobs. */
    if (valid && httpd_ws_get_fd_info(job->server, job->fd)
                 == HTTPD_WS_CLIENT_WEBSOCKET) {
        httpd_ws_frame_t frame = {
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)job->text,
            .len = job->len,
        };
        if (httpd_ws_send_frame_async(job->server, job->fd, &frame) != ESP_OK) {
            portENTER_CRITICAL(&s_lock);
            bool same_session = s_opened && !s_closing && s_server == job->server
                                && s_peer == job->peer && s_fd == job->fd;
            if (same_session) s_closing = true;
            portEXIT_CRITICAL(&s_lock);
            /* Stay on HTTPD's socket-owner task. The SDK's trigger_close API
             * queues a bare session pointer that can outlive slot reuse.
             * shutdown preserves the tracked socket until HTTPD's normal
             * close_fn owns the single close and on_close notification. */
            if (same_session) (void)shutdown(job->fd, SHUT_RDWR);
        }
    }
    portENTER_CRITICAL(&s_lock);
    remove_job_locked(job);
    portEXIT_CRITICAL(&s_lock);
    free(job);
}

static void session_closed(httpd_handle_t server, int fd)
{
    mobile_link_callbacks_t callbacks = {0};
    mobile_link_peer_t peer = 0;
    portENTER_CRITICAL(&s_lock);
    s_httpd_task = xTaskGetCurrentTaskHandle();
    if (s_server == server && s_fd == fd) {
        if (s_opened) {
            callbacks = s_callbacks;
            peer = s_peer;
        }
        s_fd = -1;
        s_peer = 0;
        s_opened = false;
        s_closing = false;
    }
    portEXIT_CRITICAL(&s_lock);
    /* A custom HTTPD close_fn takes responsibility for closing every socket,
     * including rejected/non-WebSocket HTTP sessions. */
    (void)close(fd);
    if (peer && callbacks.on_close) callbacks.on_close(peer, callbacks.ctx);
}

static esp_err_t pre_handshake(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);
    portENTER_CRITICAL(&s_lock);
    s_httpd_task = xTaskGetCurrentTaskHandle();
    bool admit = s_phase == RUNNING && s_server == req->handle
                 && s_fd < 0 && s_generation != UINT32_MAX;
    if (admit) {
        /* Never wrap the generation: a stale token can never become valid. */
        s_peer = ++s_generation;
        s_fd = fd;
        s_opened = false;
        s_closing = false;
    }
    portEXIT_CRITICAL(&s_lock);
    if (admit) return ESP_OK;
    (void)httpd_resp_set_status(req, "503 Service Unavailable");
    (void)httpd_resp_send(req, "WebSocket session unavailable", -1);
    return ESP_FAIL;
}

static esp_err_t websocket_handler(httpd_req_t *req)
{
    mobile_link_callbacks_t callbacks;
    mobile_link_peer_t peer;
    size_t max_frame;
    int fd = httpd_req_to_sockfd(req);
    portENTER_CRITICAL(&s_lock);
    bool valid = s_phase == RUNNING && s_server == req->handle
                 && s_fd == fd && s_peer != 0 && s_opened && !s_closing;
    callbacks = s_callbacks;
    peer = s_peer;
    max_frame = s_max_frame;
    portEXIT_CRITICAL(&s_lock);
    if (!valid) return ESP_FAIL;

    httpd_ws_frame_t frame = {0};
    if (httpd_ws_recv_frame(req, &frame, 0) != ESP_OK) return ESP_FAIL;
    /* HTTPD handles ping/pong/close itself. Text is deliberately unfragmented;
     * binary and continuation messages are rejected without business parsing. */
    if (frame.type != HTTPD_WS_TYPE_TEXT || !frame.final || frame.len > max_frame
        || frame.len > FRAME_LIMIT) return ESP_FAIL;
    char payload[FRAME_LIMIT + 1];
    frame.payload = (uint8_t *)payload;
    if (frame.len && httpd_ws_recv_frame(req, &frame, frame.len) != ESP_OK)
        return ESP_FAIL;
    payload[frame.len] = '\0';
    if (callbacks.on_text) callbacks.on_text(peer, payload, frame.len, callbacks.ctx);
    return ESP_OK;
}

static esp_err_t post_handshake(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);
    portENTER_CRITICAL(&s_lock);
    bool valid = s_phase == RUNNING && s_server == req->handle
                 && s_fd == fd && s_peer != 0 && !s_opened;
    mobile_link_callbacks_t callbacks = s_callbacks;
    mobile_link_peer_t peer = s_peer;
    if (valid) s_opened = true;
    portEXIT_CRITICAL(&s_lock);
    if (!valid) return ESP_FAIL;
    /* IDF 5.5.5 returns directly after upgrade and does not call the GET URI
     * handler. This callback occurs after ws_handshake_done has been set. */
    if (callbacks.on_open) callbacks.on_open(peer, callbacks.ctx);
    return ESP_OK;
}

static void stop_server(httpd_handle_t server)
{
    /* The public stop API is void. Returning with a live server would let the
     * owner free callback ctx while callbacks still use it. Preserve state and
     * retry SDK control-channel failures instead. Never run on the HTTPD task. */
    while (httpd_stop(server) != ESP_OK) vTaskDelay(1);
}

err_t mobile_link_start(const mobile_link_config_t *config,
                        const mobile_link_callbacks_t *callbacks)
{
    if (!config || !callbacks || !config->port || !config->ws_path
        || config->ws_path[0] != '/' || !config->max_frame_size
        || config->max_frame_size > FRAME_LIMIT) return ERR_PARAM;
    portENTER_CRITICAL(&s_lock);
    if (s_phase != STOPPED) {
        portEXIT_CRITICAL(&s_lock);
        return ERR_BUSY;
    }
    s_phase = STARTING;
    portEXIT_CRITICAL(&s_lock);

    size_t path_len = strlen(config->ws_path);
    char *path = malloc(path_len + 1);
    err_t result = ERR_NO_MEM;
    httpd_handle_t server = NULL;
    if (!path) goto failed;
    memcpy(path, config->ws_path, path_len + 1);
    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    /* 1024-byte frame plus strict protocol parser and newlib formatting share
     * this task. ESP32-S3 -fstack-usage shows 3040 bytes before SDK frames. */
    http_config.stack_size = 8192;
    http_config.server_port = config->port;
    http_config.close_fn = session_closed;
    if (httpd_start(&server, &http_config) != ESP_OK) {
        result = ERR_FAIL;
        goto failed;
    }
    portENTER_CRITICAL(&s_lock);
    s_server = server;
    s_callbacks = *callbacks;
    s_path = path;
    s_max_frame = config->max_frame_size;
    portEXIT_CRITICAL(&s_lock);
    httpd_uri_t uri = {
        .uri = path,
        .method = HTTP_GET,
        .handler = websocket_handler,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .ws_pre_handshake_cb = pre_handshake,
        .ws_post_handshake_cb = post_handshake,
    };
    if (httpd_register_uri_handler(server, &uri) != ESP_OK) {
        result = ERR_FAIL;
        stop_server(server);
        goto failed;
    }
    portENTER_CRITICAL(&s_lock);
    s_phase = RUNNING;
    portEXIT_CRITICAL(&s_lock);
    return ERR_OK;

failed:
    free(path);
    portENTER_CRITICAL(&s_lock);
    s_server = NULL;
    s_path = NULL;
    s_httpd_task = NULL;
    s_callbacks = (mobile_link_callbacks_t){0};
    s_phase = STOPPED;
    portEXIT_CRITICAL(&s_lock);
    return result;
}

err_t mobile_link_send_text(mobile_link_peer_t peer, const char *text, size_t len)
{
    if ((!text && len) || len > FRAME_LIMIT) return ERR_PARAM;
    portENTER_CRITICAL(&s_lock);
    bool ready = s_phase == RUNNING && s_opened && !s_closing && peer && s_peer == peer;
    size_t max_frame = s_max_frame;
    portEXIT_CRITICAL(&s_lock);
    if (!ready) return ERR_NOT_READY;
    if (len > max_frame) return ERR_PARAM;
    send_job_t *job = malloc(sizeof(*job) + len);
    if (!job) return ERR_NO_MEM;
    if (len) memcpy(job->text, text, len);
    job->peer = peer;
    job->len = len;
    portENTER_CRITICAL(&s_lock);
    if (s_phase != RUNNING || !s_opened || s_closing || s_peer != peer) {
        portEXIT_CRITICAL(&s_lock);
        free(job);
        return ERR_NOT_READY;
    }
    if (s_job_count == QUEUE_LIMIT) {
        portEXIT_CRITICAL(&s_lock);
        free(job);
        return ERR_BUSY;
    }
    job->server = s_server;
    job->fd = s_fd;
    job->next = s_jobs;
    s_jobs = job;
    ++s_job_count;
    ++s_submitters;
    portEXIT_CRITICAL(&s_lock);
    /* Stop cannot destroy this handle until every submitter exits. */
    esp_err_t queued = httpd_queue_work(job->server, send_work, job);
    portENTER_CRITICAL(&s_lock);
    --s_submitters;
    if (queued != ESP_OK) remove_job_locked(job);
    portEXIT_CRITICAL(&s_lock);
    if (queued != ESP_OK) {
        free(job);
        return ERR_FAIL;
    }
    return ERR_OK;
}

void mobile_link_stop(void)
{
    portENTER_CRITICAL(&s_lock);
    /* httpd_stop waits for its server task. Never invoke it from that task. */
    if (s_phase != RUNNING || xTaskGetCurrentTaskHandle() == s_httpd_task) {
        portEXIT_CRITICAL(&s_lock);
        return;
    }
    s_phase = STOPPING;
    httpd_handle_t server = s_server;
    portEXIT_CRITICAL(&s_lock);

    for (;;) {
        portENTER_CRITICAL(&s_lock);
        unsigned submitters = s_submitters;
        portEXIT_CRITICAL(&s_lock);
        if (!submitters) break;
        vTaskDelay(1);
    }
    /* Do not wait for queued callbacks: HTTPD may discard them on shutdown.
     * Once its task exits, jobs still on this list cannot execute anymore. */
    stop_server(server);
    portENTER_CRITICAL(&s_lock);
    send_job_t *jobs = s_jobs;
    char *path = s_path;
    s_jobs = NULL;
    s_job_count = 0;
    s_path = NULL;
    s_server = NULL;
    s_httpd_task = NULL;
    s_callbacks = (mobile_link_callbacks_t){0};
    s_peer = 0;
    s_fd = -1;
    s_opened = false;
    s_closing = false;
    /* Keep STOPPING until detached allocations are freed. */
    portEXIT_CRITICAL(&s_lock);
    while (jobs) {
        send_job_t *next = jobs->next;
        free(jobs);
        jobs = next;
    }
    free(path);
    portENTER_CRITICAL(&s_lock);
    s_phase = STOPPED;
    portEXIT_CRITICAL(&s_lock);
}
