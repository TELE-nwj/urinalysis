/* BSP 与 lwIP 各自拥有类型不同的 err_t/ERR_OK 名称。在 BSP 宏之前
 * 先引入 lwIP 的私有套接字边界,仅重命名它的 typedef。
 * 下方的公开驱动 API 保持 BSP 的 int32_t err_t 不变。 */
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

/* 临界区只保护有界的状态更新。HTTPD 调用、堆
 * 操作、等待以及用户回调始终在该锁之外执行。 */
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

    /* 这段代码运行在 HTTPD 自己的任务上,该任务同时负责套接字的关闭/升级。
     * 因此在这项检查与真正的发送之间,fd 状态不可能被复用。
     * 并发的外部 stop 会先等待 HTTPD 退出,然后才释放各作业。 */
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
            /* 留在 HTTPD 这个套接字所有者任务上。SDK 的 trigger_close API
             * 会排队一个裸会话指针,它可能比槽位复用存活得更久。
             * shutdown 会保留被跟踪的套接字,直到 HTTPD 正常的
             * close_fn 完成唯一的一次关闭与 on_close 通知。 */
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
    /* 自定义的 HTTPD close_fn 负责关闭每一个套接字,
     * 包括被拒绝的/非 WebSocket 的 HTTP 会话。 */
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
        /* 绝不让代际回绕:陈旧令牌永远不可能再次变得有效。 */
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
    /* ping/pong/close 由 HTTPD 自己处理。文本被刻意设为不分片;
     * 二进制消息与续帧消息会被直接拒绝,不做业务解析。 */
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
    /* IDF 5.5.5 在升级之后直接返回,不会调用 GET URI
     * 处理函数。本回调发生在 ws_handshake_done 已被置位之后。 */
    if (callbacks.on_open) callbacks.on_open(peer, callbacks.ctx);
    return ESP_OK;
}

static void stop_server(httpd_handle_t server)
{
    /* 公开的 stop API 返回 void。若在 server 仍存活时返回,所有者就可能
     * 在回调仍在使用 callback ctx 时将它释放。因此要保留状态并
     * 改为重试 SDK 控制通道失败。绝不在 HTTPD 任务上运行。 */
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
    /* 1024 字节的帧,加上严格的协议解析器与 newlib 格式化,共享
     * 本任务栈。ESP32-S3 的 -fstack-usage 显示 SDK 栈帧之前为 3040 字节。 */
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
    /* 在每个提交者退出之前,stop 无法销毁这个句柄。 */
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
    /* httpd_stop 会等待它自己的 server 任务。绝不在该任务上调用它。 */
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
    /* 不要等待已排队的回调:HTTPD 在关闭时可能把它们丢弃。
     * 一旦其任务退出,仍留在该链表上的作业就再也无法执行。 */
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
    /* 在分离出去的分配被释放之前,一直保持 STOPPING。 */
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
