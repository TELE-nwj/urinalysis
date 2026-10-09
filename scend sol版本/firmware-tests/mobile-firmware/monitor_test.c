#include <stddef.h>
#include <stdarg.h>
#include "app/monitor/monitor.h"
#include "service/input/input.h"
#include "service/storage/storage.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
static uint64_t now_ms;
static unsigned reset_count, sync_count, completions;
static uint64_t storage_delay_ms;
static int depth, violations;
static int task_create_fail;
static input_cb_t key_cb;
static urine_meas_t measurement;
static void (*task_entry)(void *);
void *memcpy(void *d,const void *s,size_t n) { char *a=d;const char*b=s;while(n--)*a++=*b++;return d; }
void *memset(void *d,int c,size_t n) { char*a=d;while(n--)*a++=(char)c;return d; }
size_t strlen(const char*s) {size_t n=0;while(s[n])n++;return n;}
int strcmp(const char*a,const char*b) {while(*a&&*a==*b)a++,b++;return (unsigned char)*a-(unsigned char)*b;}
int snprintf(char*out,size_t n,const char*fmt,...) {va_list ap;va_start(ap,fmt);if(fmt[0]=='%'&&fmt[1]=='s'){const char*s=va_arg(ap,const char*);size_t z=strlen(s);if(n){size_t k=z<n-1?z:n-1;memcpy(out,s,k);out[k]=0;}va_end(ap);return (int)z;}if(n)out[0]=0;va_end(ap);return 0;}
void test_lock(portMUX_TYPE*m){(void)m;if(depth++)violations++;}
void test_unlock(portMUX_TYPE*m){(void)m;if(--depth)violations++;}
BaseType_t xTaskCreate(void(*fn)(void*),const char*n,unsigned s,void*a,unsigned p,TaskHandle_t*t){(void)n;(void)s;(void)a;(void)p;if(task_create_fail)return 0;task_entry=fn;if(t)*t=(void*)1;return pdPASS;}
void vTaskDelay(unsigned n){(void)n;}
void vTaskDelete(TaskHandle_t t){(void)t;}
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void){return (void*)1;}
int xSemaphoreTakeRecursive(SemaphoreHandle_t m,unsigned n){(void)m;(void)n;return 1;}
int xSemaphoreGiveRecursive(SemaphoreHandle_t m){(void)m;return 1;}
int esp_log_level_get(const char*s){(void)s;return 3;}
void esp_log_level_set(const char*s,int n){(void)s;(void)n;}
void test_log(const char*t,const char*f,...){(void)t;(void)f;}
uint64_t clock_ms(void){return now_ms;}
void clock_now_hms(char*b,size_t n){if(n>=9)memcpy(b,"12:34:56",9);}
void clock_sync_hms(int h,int m,int s){(void)h;(void)m;(void)s;if(depth)violations++;sync_count++;}
void power_poll(void){}
int power_percent(void){return -1;}
err_t input_init(input_cb_t cb){key_cb=cb;return ERR_OK;}
void input_poll(void){}
const urine_meas_t*urine_get(void){return &measurement;}
void urine_reset(void){if(depth)violations++;reset_count++;memset(&measurement,0,sizeof measurement);}
void urine_capture_hold(void){}
void urine_capture_clear(void){}
void urine_capture_arm(void){}
void urine_poll(void){}
void urine_rate_zero(void){measurement.rate_mlh=0;}
void screen_show_urine(float f){(void)f;}
void screen_show_rate(float f){(void)f;}
void screen_show_drops(uint32_t n){(void)n;}
void screen_show_elapsed(const char*s){(void)s;}
void screen_show_status(const char*s){(void)s;}
void screen_show_alert(const char*s){(void)s;}
void screen_show_battery(int n){(void)n;}
void screen_show_clock(const char*s){(void)s;}
err_t storage_create_session(char*b,size_t n){(void)n;now_ms+=storage_delay_ms;memcpy(b,"U0000001",9);return ERR_OK;}
err_t storage_ensure_session(const char*s){(void)s;return ERR_OK;}
err_t storage_open_session(const char*s){(void)s;return ERR_OK;}
err_t storage_write_open(const char*s){(void)s;return ERR_OK;}
err_t storage_close_session(void){return ERR_OK;}
#ifndef RED_BASELINE
bool storage_get_status(storage_status_t*out){out->state=STORAGE_UNKNOWN;out->checked=false;out->checked_uptime_ms=0;return true;}
#endif
#define TAG S1_TAG
#include "app/S1/S1.c"
#undef TAG
#include "app/S2/S2.c"
#ifdef RED_BASELINE
#include "build-host/baseline_monitor.c"
#else
#include "app/monitor/monitor.c"
#endif
#define CHECK(x) do{if(!(x))return __LINE__;}while(0)
int test_external_reset_is_queued(void){CHECK(app_monitor_start()==ERR_OK);unsigned n=reset_count;app_monitor_reset();CHECK(reset_count==n);return 0;}
#ifndef RED_BASELINE
static app_completion_t last;
static app_completion_t history[16];
static void completed(const app_completion_t*c,void*ctx){(void)ctx;if(depth)violations++;last=*c;if(completions<16)history[completions]=*c;completions++;app_snapshot_t snap;if(!app_monitor_get_snapshot(&snap)||snap.state_seq<c->state_seq)violations++;}
static err_t submit(app_command_type_t type,uint64_t token,uint64_t expiry){app_command_t c={.type=type,.token=token,.expires_at_ms=expiry};return app_monitor_submit_command(&c);}
int test_owner_commands_and_snapshot(void){app_snapshot_t snap;CHECK(!app_monitor_get_snapshot(&snap));CHECK(app_monitor_set_completion_callback(completed,0)==ERR_OK);CHECK(app_monitor_start()==ERR_OK);CHECK(app_monitor_get_snapshot(&snap));CHECK(snap.state==APP_IDLE&&snap.battery_percent==-1&&snap.storage_state==STORAGE_UNKNOWN);CHECK(submit(APP_COMMAND_START,1,5000)==ERR_OK);CHECK(app_monitor_state()==APP_IDLE);monitor_step();CHECK(last.token==1&&last.result==APP_COMMAND_OK&&last.state_seq==1&&last.state==APP_MEASURING);CHECK(submit(APP_COMMAND_START,2,5000)==ERR_OK);monitor_step();CHECK(last.state_seq==1);now_ms=1700;monitor_step();CHECK(app_monitor_get_snapshot(&snap));CHECK(snap.elapsed_s==1);CHECK(submit(APP_COMMAND_PAUSE,3,5000)==ERR_OK);monitor_step();CHECK(last.state==APP_PAUSED&&last.state_seq==2);now_ms=101700;monitor_step();CHECK(app_monitor_get_snapshot(&snap));CHECK(snap.elapsed_s==1);CHECK(submit(APP_COMMAND_START,4,106700)==ERR_OK);monitor_step();now_ms+=1300;monitor_step();CHECK(app_monitor_get_snapshot(&snap));CHECK(snap.elapsed_s==3);CHECK(submit(APP_COMMAND_RESET,5,now_ms+5000)==ERR_OK);monitor_step();CHECK(last.state==APP_IDLE&&last.state_seq==4);CHECK(app_monitor_get_snapshot(&snap)&&snap.elapsed_s==0);CHECK(violations==0);return 0;}
int test_capacity_expiry_and_time_sync(void){CHECK(app_monitor_set_completion_callback(completed,0)==ERR_OK);CHECK(app_monitor_start()==ERR_OK);for(unsigned n=0;n<8;n++)CHECK(submit(APP_COMMAND_PAUSE,n+1,5000)==ERR_OK);CHECK(submit(APP_COMMAND_START,9,5000)==ERR_BUSY);now_ms=5000;monitor_step();CHECK(completions==8&&last.result==APP_COMMAND_EXPIRED&&last.state_seq==0);CHECK(app_monitor_state()==APP_IDLE);app_command_t c={.type=APP_COMMAND_SYNC,.token=10,.expires_at_ms=10000,.hour=24};CHECK(app_monitor_submit_command(&c)==ERR_PARAM);c.hour=23;c.minute=59;c.second=59;CHECK(app_monitor_submit_command(&c)==ERR_OK);monitor_step();CHECK(sync_count==1&&last.result==APP_COMMAND_OK&&last.state_seq==0);app_snapshot_t snap;CHECK(app_monitor_get_snapshot(&snap)&&snap.time_synced);CHECK(app_monitor_set_completion_callback(completed,0)==ERR_BUSY);CHECK(violations==0);return 0;}
int test_key_uses_common_state_machine(void){CHECK(app_monitor_start()==ERR_OK);key_cb(INPUT_KEY1);CHECK(app_monitor_state()==APP_MEASURING);key_cb(INPUT_KEY1);CHECK(app_monitor_state()==APP_PAUSED);key_cb(INPUT_KEY1);CHECK(app_monitor_state()==APP_MEASURING);key_cb(INPUT_KEY2);app_snapshot_t snap;CHECK(app_monitor_get_snapshot(&snap));CHECK(snap.state==APP_IDLE&&snap.state_seq==4&&snap.elapsed_s==0);return 0;}
int test_snapshot_identity_and_elapsed_over_24h(void){CHECK(app_monitor_start()==ERR_OK);app_snapshot_t a,b;CHECK(app_monitor_get_snapshot(&a));CHECK(app_monitor_get_snapshot(&b)&&a.seq==b.seq&&a.sample_uptime_ms==b.sample_uptime_ms);key_cb(INPUT_KEY1);now_ms=90061000;monitor_step();CHECK(app_monitor_get_snapshot(&a));CHECK(a.elapsed_s==90061&&a.seq>b.seq);key_cb(INPUT_KEY1);uint64_t frozen=a.elapsed_s;now_ms+=90000000;monitor_step();CHECK(app_monitor_get_snapshot(&b)&&b.elapsed_s==frozen);CHECK(violations==0);return 0;}
#include "service/mobile/mobile.h"
#include "app/mobile/mobile.h"
static service_mobile_config_t mobile_config;
static unsigned mobile_completed;
static service_mobile_result_t mobile_result;
static uint64_t mobile_token, mobile_state_seq;
err_t service_mobile_start(const service_mobile_config_t*c){mobile_config=*c;return ERR_OK;}
void service_mobile_stop(void){}
void service_mobile_tick(void){}
void service_mobile_complete(uint64_t t,service_mobile_result_t r,service_mobile_state_t s,uint64_t seq){(void)s;if(depth)violations++;mobile_completed++;mobile_result=r;mobile_token=t;mobile_state_seq=seq;}
#ifdef RED_APP
err_t app_mobile_init(const app_mobile_config_t*c){(void)c;return ERR_FAIL;}
err_t app_mobile_start(void){return ERR_FAIL;}
#else
#include "app/mobile/mobile.c"
#endif
int test_mobile_adapter_uses_monitor_queue_and_permanent_callback(void){app_mobile_config_t c={.device_id="NIAIO-test",.ap_ssid="NIAIO-Monitor",.ap_password="niaio-v1-test",.ap_channel=1,.ap_max_connections=1};CHECK(app_mobile_init(&c)==ERR_OK);CHECK(app_mobile_init(&c)==ERR_BUSY);CHECK(app_monitor_start()==ERR_OK);CHECK(app_mobile_start()==ERR_OK);service_mobile_request_t req={.token=42,.command=SERVICE_MOBILE_START,.expires_uptime_ms=5000};CHECK(mobile_config.submit(&req,mobile_config.ctx)==ERR_OK);CHECK(app_monitor_state()==APP_IDLE);monitor_step();CHECK(mobile_completed==1&&mobile_result==SERVICE_MOBILE_OK&&mobile_token==42&&mobile_state_seq==1);service_mobile_snapshot_t snap;CHECK(mobile_config.snapshot(&snap,mobile_config.ctx)&&snap.state==SERVICE_MOBILE_MEASURING&&snap.battery_percent==-1&&!snap.storage_checked_known);req.command=SERVICE_MOBILE_TIME_SYNC;req.hour=18;req.minute=30;req.second=25;req.token=43;CHECK(mobile_config.submit(&req,mobile_config.ctx)==ERR_OK);monitor_step();CHECK(sync_count==1&&mobile_completed==2&&mobile_state_seq==1);CHECK(mobile_config.snapshot(&snap,mobile_config.ctx)&&snap.time_synced);req.command=(service_mobile_command_t)99;CHECK(mobile_config.submit(&req,mobile_config.ctx)==ERR_PARAM);CHECK(violations==0);return 0;}
int test_failed_monitor_start_is_not_ready(void){task_create_fail=1;CHECK(app_monitor_start()==ERR_FAIL);app_snapshot_t snap;CHECK(!app_monitor_get_snapshot(&snap));CHECK(submit(APP_COMMAND_START,1,5000)==ERR_NOT_READY);task_create_fail=0;CHECK(app_monitor_start()==ERR_OK);CHECK(app_monitor_get_snapshot(&snap));return 0;}
int test_started_storage_transaction_finishes_and_next_request_expires(void){CHECK(app_monitor_set_completion_callback(completed,0)==ERR_OK);CHECK(app_monitor_start()==ERR_OK);storage_delay_ms=6000;CHECK(submit(APP_COMMAND_START,1,5000)==ERR_OK);CHECK(submit(APP_COMMAND_PAUSE,2,5000)==ERR_OK);monitor_step();CHECK(completions==2);CHECK(history[0].token==1&&history[0].result==APP_COMMAND_OK);CHECK(history[1].token==2&&history[1].result==APP_COMMAND_EXPIRED);CHECK(app_monitor_state()==APP_MEASURING&&last.state_seq==1);CHECK(violations==0);return 0;}
int test_time_sync_does_not_change_effective_elapsed(void){CHECK(app_monitor_start()==ERR_OK);key_cb(INPUT_KEY1);now_ms=2345;monitor_step();app_snapshot_t before,after;CHECK(app_monitor_get_snapshot(&before)&&before.elapsed_s==2);app_command_t c={.type=APP_COMMAND_SYNC,.token=1,.expires_at_ms=now_ms+5000,.hour=23,.minute=59,.second=59};CHECK(app_monitor_submit_command(&c)==ERR_OK);monitor_step();CHECK(app_monitor_get_snapshot(&after));CHECK(after.elapsed_s==before.elapsed_s&&after.state_seq==before.state_seq&&after.time_synced);key_cb(INPUT_KEY1);now_ms+=90000000;CHECK(app_monitor_submit_command(&(app_command_t){.type=APP_COMMAND_SYNC,.token=2,.expires_at_ms=now_ms+5000,.hour=0})==ERR_OK);monitor_step();CHECK(app_monitor_get_snapshot(&after)&&after.elapsed_s==2);return 0;}
#endif
