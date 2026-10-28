#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define CONFIG_HTTPD_WS_SUPPORT 1
#define CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT 1
#define CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT 1
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
typedef void *httpd_handle_t;
typedef struct { httpd_handle_t handle; int method; int fd; } httpd_req_t;
typedef enum { HTTPD_WS_TYPE_CONTINUE=0, HTTPD_WS_TYPE_TEXT=1,
 HTTPD_WS_TYPE_BINARY=2, HTTPD_WS_TYPE_CLOSE=8, HTTPD_WS_TYPE_PING=9,
 HTTPD_WS_TYPE_PONG=10 } httpd_ws_type_t;
typedef struct { bool final; bool fragmented; httpd_ws_type_t type;
 uint8_t *payload; size_t len; } httpd_ws_frame_t;
typedef enum { HTTPD_WS_CLIENT_INVALID, HTTPD_WS_CLIENT_HTTP,
 HTTPD_WS_CLIENT_WEBSOCKET } httpd_ws_client_info_t;
typedef struct { unsigned server_port; unsigned max_open_sockets; unsigned stack_size;
 void (*close_fn)(httpd_handle_t,int); } httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){80,7,4096,0})
#define HTTP_GET 0
typedef struct { const char *uri; int method; esp_err_t (*handler)(httpd_req_t*);
 void *user_ctx; bool is_websocket; bool handle_ws_control_frames;
 esp_err_t (*ws_pre_handshake_cb)(httpd_req_t*);
 esp_err_t (*ws_post_handshake_cb)(httpd_req_t*); } httpd_uri_t;
typedef void (*httpd_work_fn_t)(void*);
esp_err_t httpd_start(httpd_handle_t*,const httpd_config_t*);
esp_err_t httpd_stop(httpd_handle_t);
esp_err_t httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t*);
esp_err_t httpd_queue_work(httpd_handle_t,httpd_work_fn_t,void*);
int httpd_req_to_sockfd(httpd_req_t*);
esp_err_t httpd_ws_recv_frame(httpd_req_t*,httpd_ws_frame_t*,size_t);
esp_err_t httpd_ws_send_frame_async(httpd_handle_t,int,httpd_ws_frame_t*);
httpd_ws_client_info_t httpd_ws_get_fd_info(httpd_handle_t,int);
esp_err_t httpd_resp_set_status(httpd_req_t*,const char*);
esp_err_t httpd_resp_send(httpd_req_t*,const char*,int);
esp_err_t httpd_sess_trigger_close(httpd_handle_t,int);
