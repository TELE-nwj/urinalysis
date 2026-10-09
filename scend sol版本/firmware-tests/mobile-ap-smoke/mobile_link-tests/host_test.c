#include "mobile_link.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"

static const char *current_test = "initialization";
static int lock_depth, lock_errors, callback_errors, allocations;
static unsigned char heap[262144];
static size_t heap_used;
static void *allocated[512];
static unsigned allocated_count;
static httpd_uri_t uri;
static httpd_config_t cfg;
static httpd_ws_client_info_t sockets[32];
static TaskHandle_t task = (void*)1;
static int open_count, close_count, text_count, sent_count, last_sent_fd;
static int shutdown_count, shutdown_fd, async_close_requests;
static int start_fail, register_fail, queue_fail, send_fail, stop_fail_once;
static int malloc_fail, run_work_inline, recv_fail;
static mobile_link_peer_t latest_peer;
static char sent_text[1025];
static size_t sent_len;
static httpd_ws_frame_t incoming;
static char input[1025];
static int read_payloads, response_status, stop_from_callback, callback_send;
static err_t callback_send_result;
static struct { httpd_work_fn_t fn; void *arg; } queued[16];
static int queue_count;

const char *test_name(void) { return current_test; }
void *memcpy(void *d,const void *s,size_t n)
{ unsigned char *a=d; const unsigned char *b=s; while(n--) *a++=*b++; return d; }
void *memset(void *d,int v,size_t n)
{ unsigned char *a=d; while(n--) *a++=(unsigned char)v; return d; }
size_t strlen(const char *s) { size_t n=0; while(s[n]) n++; return n; }
void *malloc(size_t n)
{
    if(malloc_fail) return 0;
    n=(n+15)&~(size_t)15;
    if(heap_used+n>sizeof(heap)||allocated_count==512) return 0;
    void *p=heap+heap_used; heap_used+=n;
    allocated[allocated_count++]=p; allocations++; return p;
}
void free(void *p)
{
    if(!p) return;
    for(unsigned i=0;i<allocated_count;i++) if(allocated[i]==p) {
        allocated[i]=0; allocations--; return;
    }
    lock_errors++; /* Double free or foreign pointer. */
}
void test_lock(portMUX_TYPE *m)
{ (void)m; if(lock_depth++) lock_errors++; }
void test_unlock(portMUX_TYPE *m)
{ (void)m; if(--lock_depth) lock_errors++; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return task; }
void vTaskDelay(unsigned n) { (void)n; }
int close(int fd) { sockets[fd]=HTTPD_WS_CLIENT_INVALID; return 0; }
int shutdown(int fd,int how)
{
    if(task!=(void*)2 || lock_depth || how!=2
       || sockets[fd]!=HTTPD_WS_CLIENT_WEBSOCKET) callback_errors++;
    shutdown_count++; shutdown_fd=fd;
    /* shutdown does not delete HTTPD's session or run its close callback. */
    return 0;
}
esp_err_t httpd_start(httpd_handle_t *h,const httpd_config_t *c)
{ if(start_fail) return ESP_FAIL; cfg=*c; *h=(void*)0x1234; return ESP_OK; }
static void disconnect(int fd)
{
    TaskHandle_t old=task; task=(void*)2;
    cfg.close_fn((void*)0x1234,fd); task=old;
}
esp_err_t httpd_stop(httpd_handle_t h)
{
    (void)h;
    if(lock_depth) lock_errors++;
    if(stop_fail_once) { stop_fail_once=0; return ESP_FAIL; }
    for(int i=0;i<32;i++) if(sockets[i]!=HTTPD_WS_CLIENT_INVALID) disconnect(i);
    /* Deliberately abandon queued work: driver must reclaim after stop. */
    queue_count=0;
    return ESP_OK;
}
esp_err_t httpd_register_uri_handler(httpd_handle_t h,const httpd_uri_t *u)
{ (void)h; if(register_fail) return ESP_FAIL; uri=*u; return ESP_OK; }
esp_err_t httpd_queue_work(httpd_handle_t h,httpd_work_fn_t fn,void *arg)
{
    (void)h; if(lock_depth) lock_errors++;
    if(queue_fail||queue_count==16) return ESP_FAIL;
    if(run_work_inline) {
        TaskHandle_t old=task; task=(void*)2; fn(arg); task=old;
        return ESP_OK;
    }
    queued[queue_count].fn=fn; queued[queue_count++].arg=arg; return ESP_OK;
}
static void drain(void)
{
    TaskHandle_t old=task; task=(void*)2;
    while(queue_count) {
        httpd_work_fn_t fn=queued[0].fn; void *arg=queued[0].arg;
        queue_count--;
        for(int i=0;i<queue_count;i++) queued[i]=queued[i+1];
        fn(arg);
    }
    task=old;
}
int httpd_req_to_sockfd(httpd_req_t *r) { return r->fd; }
esp_err_t httpd_ws_recv_frame(httpd_req_t *r,httpd_ws_frame_t *f,size_t n)
{
    (void)r;
    if(recv_fail) return ESP_FAIL;
    if(!n) { *f=incoming; return ESP_OK; }
    read_payloads++;
    if(n<incoming.len) return ESP_FAIL;
    memcpy(f->payload,input,incoming.len); return ESP_OK;
}
esp_err_t httpd_ws_send_frame_async(httpd_handle_t h,int fd,httpd_ws_frame_t *f)
{
    (void)h; if(lock_depth) lock_errors++;
    if(send_fail) return ESP_FAIL;
    if(sockets[fd]!=HTTPD_WS_CLIENT_WEBSOCKET) callback_errors++;
    sent_count++; last_sent_fd=fd; sent_len=f->len;
    memcpy(sent_text,f->payload,f->len); return ESP_OK;
}
httpd_ws_client_info_t httpd_ws_get_fd_info(httpd_handle_t h,int fd)
{ (void)h; return sockets[fd]; }
esp_err_t httpd_resp_set_status(httpd_req_t *r,const char *s)
{ (void)r; response_status=s[0]=='5'?503:0; return ESP_OK; }
esp_err_t httpd_resp_send(httpd_req_t *r,const char *s,int n)
{ (void)r; (void)s; (void)n; return ESP_OK; }
esp_err_t httpd_sess_trigger_close(httpd_handle_t h,int fd)
{ (void)h; async_close_requests++; disconnect(fd); return ESP_OK; }
static void on_open(mobile_link_peer_t p,void *ctx)
{
    if(lock_depth||ctx!=(void*)0x5678) callback_errors++;
    latest_peer=p; open_count++;
    if(callback_send) callback_send_result=mobile_link_send_text(p,"hello",5);
    if(stop_from_callback) mobile_link_stop();
}
static void on_text(mobile_link_peer_t p,const char *s,size_t n,void *ctx)
{
    if(lock_depth||ctx!=(void*)0x5678||p!=latest_peer||n!=incoming.len
       ||(n&&s[0]!='x')) callback_errors++;
    text_count++;
    if(callback_send) callback_send_result=mobile_link_send_text(p,s,n);
    if(stop_from_callback) mobile_link_stop();
}
static void on_close(mobile_link_peer_t p,void *ctx)
{
    if(lock_depth||ctx!=(void*)0x5678||!p) callback_errors++;
    close_count++;
    if(callback_send) callback_send_result=mobile_link_send_text(p,"bye",3);
    if(stop_from_callback) mobile_link_stop();
}
static esp_err_t connect_peer(int fd)
{
    TaskHandle_t old=task; task=(void*)2;
    sockets[fd]=HTTPD_WS_CLIENT_HTTP;
    httpd_req_t r={(void*)0x1234,HTTP_GET,fd};
    esp_err_t result=uri.ws_pre_handshake_cb(&r);
    if(result==ESP_OK) {
        sockets[fd]=HTTPD_WS_CLIENT_WEBSOCKET;
        /* IDF 5.5.5 does not call URI handler for the successful upgrade GET. */
        if(uri.ws_post_handshake_cb) result=uri.ws_post_handshake_cb(&r);
    }
    if(result!=ESP_OK) disconnect(fd);
    task=old; return result;
}
static esp_err_t receive(int fd,httpd_ws_type_t type,size_t n,bool final)
{
    TaskHandle_t old=task; task=(void*)2;
    incoming=(httpd_ws_frame_t){.type=type,.len=n,.final=final};
    memset(input,'x',sizeof(input));
    httpd_req_t r={(void*)0x1234,1,fd};
    esp_err_t result=uri.handler(&r);
    if(result!=ESP_OK) disconnect(fd);
    task=old; return result;
}
#define CHECK(x) do { if(!(x)) return __LINE__; } while(0)
#define CASE(name) do { current_test=(name); } while(0)
int run_tests(void)
{
    mobile_link_config_t c={80,"/ws",1024};
    mobile_link_callbacks_t b={on_open,on_text,on_close,(void*)0x5678};
    CASE("start validates configuration");
    CHECK(mobile_link_start(0,&b)==ERR_PARAM);
    CHECK(mobile_link_start(&c,0)==ERR_PARAM);
    c.max_frame_size=1025; CHECK(mobile_link_start(&c,&b)==ERR_PARAM);
    c.max_frame_size=0; CHECK(mobile_link_start(&c,&b)==ERR_PARAM);
    c.max_frame_size=1024; c.port=0; CHECK(mobile_link_start(&c,&b)==ERR_PARAM);
    c.port=80; c.ws_path="ws"; CHECK(mobile_link_start(&c,&b)==ERR_PARAM);
    c.ws_path="/ws";
    CASE("start failure and registration rollback reclaim memory");
    malloc_fail=1; CHECK(mobile_link_start(&c,&b)==ERR_NO_MEM); malloc_fail=0;
    CHECK(allocations==0);
    start_fail=1; CHECK(mobile_link_start(&c,&b)==ERR_FAIL); start_fail=0;
    CHECK(allocations==0);
    register_fail=1; stop_fail_once=1;
    CHECK(mobile_link_start(&c,&b)==ERR_FAIL); register_fail=0;
    CHECK(allocations==0);
    CASE("start and callback send without driver lock");
    CHECK(mobile_link_start(&c,&b)==ERR_OK);
    CHECK(mobile_link_start(&c,&b)==ERR_BUSY);
    CHECK(cfg.server_port==80);
    CHECK(cfg.stack_size==8192);
    callback_send=1; stop_from_callback=1;
    CHECK(connect_peer(7)==ESP_OK); CHECK(open_count==1&&latest_peer!=0);
    CHECK(callback_send_result==ERR_OK); drain();
    CHECK(sent_count==1&&sent_len==5&&last_sent_fd==7);
    CHECK(callback_errors==0&&lock_errors==0);
    CASE("second session rejected before WebSocket upgrade");
    CHECK(connect_peer(8)!=ESP_OK); CHECK(response_status==503);
    CHECK(open_count==1&&close_count==0);
    CASE("1024 byte text accepted; echo payload copied");
    CHECK(receive(7,HTTPD_WS_TYPE_TEXT,1024,true)==ESP_OK);
    CHECK(text_count==1&&callback_send_result==ERR_OK);
    input[0]='y'; drain(); CHECK(sent_len==1024&&sent_text[0]=='x');
    CASE("1025 byte text rejected before payload receive");
    int reads=read_payloads;
    CHECK(receive(7,HTTPD_WS_TYPE_TEXT,1025,true)!=ESP_OK);
    CHECK(read_payloads==reads&&text_count==1&&close_count==1);
    mobile_link_peer_t old=latest_peer;
    CHECK(mobile_link_send_text(old,"a",1)==ERR_NOT_READY);
    callback_send=0; stop_from_callback=0;
    CASE("fd reuse rejects previous generation and queued delivery");
    CHECK(connect_peer(7)==ESP_OK); mobile_link_peer_t p2=latest_peer;
    CHECK(p2!=old); CHECK(mobile_link_send_text(old,"old",3)==ERR_NOT_READY);
    CHECK(mobile_link_send_text(p2,"stale",5)==ERR_OK);
    disconnect(7); CHECK(connect_peer(7)==ESP_OK); CHECK(latest_peer!=p2);
    int sent=sent_count; drain(); CHECK(sent_count==sent);
    CASE("async worker validates current WebSocket fd state");
    CHECK(mobile_link_send_text(latest_peer,"a",1)==ERR_OK);
    sockets[7]=HTTPD_WS_CLIENT_HTTP; drain(); CHECK(sent_count==sent);
    sockets[7]=HTTPD_WS_CLIENT_WEBSOCKET;
    CASE("send validation, backpressure, queue failure cleanup");
    CHECK(mobile_link_send_text(latest_peer,0,1)==ERR_PARAM);
    CHECK(mobile_link_send_text(latest_peer,"a",1025)==ERR_PARAM);
    malloc_fail=1; CHECK(mobile_link_send_text(latest_peer,"a",1)==ERR_NO_MEM);
    malloc_fail=0;
    queue_fail=1; int live=allocations;
    CHECK(mobile_link_send_text(latest_peer,"a",1)==ERR_FAIL);
    CHECK(allocations==live); queue_fail=0;
    for(int i=0;i<4;i++) CHECK(mobile_link_send_text(latest_peer,"q",1)==ERR_OK);
    CHECK(mobile_link_send_text(latest_peer,"q",1)==ERR_BUSY); drain();
    CASE("work may complete before queue submission returns");
    run_work_inline=1; sent=sent_count;
    CHECK(mobile_link_send_text(latest_peer,"inline",6)==ERR_OK);
    CHECK(sent_count==sent+1); run_work_inline=0;
    CASE("send failure closes the peer without locking callbacks");
    send_fail=1; old=latest_peer; int closes_before_send=close_count;
    CHECK(mobile_link_send_text(old,"fail",4)==ERR_OK);
    CHECK(mobile_link_send_text(old,"queued stale",12)==ERR_OK);
    drain(); send_fail=0;
    CHECK(async_close_requests==0);
    CHECK(shutdown_count==1&&shutdown_fd==7&&callback_errors==0);
    CHECK(close_count==closes_before_send);
    CHECK(mobile_link_send_text(old,"a",1)==ERR_NOT_READY);
    disconnect(7); CHECK(close_count==closes_before_send+1);
    CHECK(connect_peer(7)==ESP_OK);
    CASE("fragmented and binary frames close without text dispatch");
    CHECK(receive(7,HTTPD_WS_TYPE_TEXT,3,false)!=ESP_OK);
    CHECK(connect_peer(7)==ESP_OK);
    CHECK(receive(7,HTTPD_WS_TYPE_BINARY,3,true)!=ESP_OK); CHECK(text_count==1);
    CASE("receive error closes session exactly once");
    CHECK(connect_peer(7)==ESP_OK); int closes=close_count;
    recv_fail=1; CHECK(receive(7,HTTPD_WS_TYPE_TEXT,3,true)!=ESP_OK); recv_fail=0;
    CHECK(close_count==closes+1);
    CASE("empty text accepted and server stop reclaims abandoned work");
    CHECK(connect_peer(7)==ESP_OK);
    CHECK(receive(7,HTTPD_WS_TYPE_TEXT,0,true)==ESP_OK); CHECK(text_count==2);
    old=latest_peer;
    CHECK(mobile_link_send_text(old,"pending",7)==ERR_OK);
    callback_send=1; stop_from_callback=1;
    stop_fail_once=1; mobile_link_stop();
    CHECK(callback_send_result==ERR_NOT_READY);
    CHECK(allocations==0&&queue_count==0);
    CHECK(mobile_link_send_text(old,"a",1)==ERR_NOT_READY);
    CASE("restart does not recycle generation tokens");
    callback_send=0; stop_from_callback=0;
    c.max_frame_size=16; CHECK(mobile_link_start(&c,&b)==ERR_OK);
    CHECK(connect_peer(7)==ESP_OK); CHECK(latest_peer!=old);
    CHECK(mobile_link_send_text(latest_peer,"a",17)==ERR_PARAM);
    CHECK(receive(7,HTTPD_WS_TYPE_TEXT,17,true)!=ESP_OK);
    mobile_link_stop(); mobile_link_stop();
    CHECK(allocations==0&&lock_errors==0&&callback_errors==0);
    return 0;
}
