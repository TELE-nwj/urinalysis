#include "service/mobile/mobile.h"
#include "driver/mobile_link/mobile_link.h"
#include "driver/wifi/wifi.h"
#include "freertos/semphr.h"
#include "string.h"
#include "stdio.h"
#include <stdarg.h>

static mobile_link_callbacks_t callbacks;
static int lock_depth, lock_errors, submits, sends, submit_error, sync_complete, snapshot_unknown, send_error;
static int mutex_count, send_depth, race_complete, race_arm, downgrade_errors, saw_completed;
static int hello_queued, hello_order_errors, open_tick_arm;
static int wifi_error, link_error, wifi_stops, link_stops;
static uint64_t now = 1000;
static service_mobile_request_t submitted;
static service_mobile_snapshot_t sample;
static char sent[1025];
static uint32_t random_value;
static char session[65];
void *memcpy(void *d,const void *s,size_t n) {char *a=d;const char *b=s;while(n--)*a++=*b++;return d;}
void *memset(void *d,int v,size_t n) {char *a=d;while(n--)*a++=(char)v;return d;}
size_t strlen(const char *s) {size_t n=0;while(s[n])n++;return n;}
int strcmp(const char *a,const char *b) {while(*a&&*a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
int memcmp(const void *a,const void *b,size_t n) {const char *x=a,*y=b;while(n--){if(*x!=*y)return *x-*y;x++;y++;}return 0;}
static int contains(const char *s,const char *q) {for(;*s;s++){size_t i=0;while(q[i]&&s[i]==q[i])i++;if(!q[i])return 1;}return 0;}
static int host_double(char *out,double value)
{
    uint64_t whole=(uint64_t)value;unsigned frac=(unsigned)((value-(double)whole)*1000000+0.5);
    char tmp[32];unsigned z=0;do{tmp[z++]=(char)('0'+whole%10);whole/=10;}while(whole);
    unsigned n=0;while(z)out[n++]=tmp[--z];
    if(frac){out[n++]='.';unsigned base=100000;while(base){out[n++]=(char)('0'+frac/base);frac%=base;base/=10;}while(out[n-1]=='0')n--;}
    out[n]=0;return (int)n;
}
int snprintf(char *out,size_t cap,const char *fmt,...)
{
    char full[4096];size_t n=0;va_list ap;va_start(ap,fmt);
    while(*fmt){
        if(*fmt!='%'){full[n++]=*fmt++;continue;}fmt++;
        if(*fmt=='s'){const char *s=va_arg(ap,const char*);while(*s)full[n++]=*s++;fmt++;continue;}
        if(*fmt=='.'){fmt+=4;char tmp[64];int z=host_double(tmp,va_arg(ap,double));for(int i=0;i<z;i++)full[n++]=tmp[i];continue;}
        unsigned width=0; if(*fmt=='0'){fmt++;width=(unsigned)(*fmt++-'0');}
        int wide=0;if(fmt[0]=='l'&&fmt[1]=='l'){wide=1;fmt+=2;}
        int hex=*fmt=='x';fmt++; uint64_t v=wide?va_arg(ap,unsigned long long):va_arg(ap,unsigned int);
        char tmp[32];unsigned z=0;do{unsigned d=(unsigned)(v%(hex?16:10));tmp[z++]=(char)(d<10?'0'+d:'a'+d-10);v/=(hex?16:10);}while(v);
        while(z<width)tmp[z++]='0';
        while(z)full[n++]=tmp[--z];
    }va_end(ap);if(cap){size_t z=n<cap-1?n:cap-1;memcpy(out,full,z);out[z]=0;}return (int)n;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void){return (void*)(uintptr_t)++mutex_count;}
int xSemaphoreTake(SemaphoreHandle_t m,uint32_t t){(void)t;if((uintptr_t)m==1){if(lock_depth++)lock_errors++;}else if(send_depth++)lock_errors++;return 1;}
int xSemaphoreGive(SemaphoreHandle_t m){if((uintptr_t)m==1){if(--lock_depth)lock_errors++;if(race_arm){race_arm=0;service_mobile_complete(submitted.token,SERVICE_MOBILE_OK,SERVICE_MOBILE_MEASURING,4);}if(open_tick_arm){open_tick_arm=0;service_mobile_tick();}}else if(--send_depth)lock_errors++;return 1;}
uint32_t esp_random(void){return ++random_value;}
err_t wifi_ap_start(const wifi_ap_config_t *c){(void)c;return wifi_error;}
void wifi_ap_stop(void){wifi_stops++;}
bool wifi_ap_is_running(void){return false;}
err_t mobile_link_start(const mobile_link_config_t *c,const mobile_link_callbacks_t *cb){(void)c;if(link_error)return link_error;callbacks=*cb;return ERR_OK;}
void mobile_link_stop(void){link_stops++;}
err_t mobile_link_send_text(mobile_link_peer_t p,const char *s,size_t n){(void)p;if(lock_depth)lock_errors++;sends++;memcpy(sent,s,n);sent[n]=0;if(!send_error){if(contains(sent,"\"type\":\"hello\""))hello_queued=1;else if(!hello_queued)hello_order_errors++;}if(race_complete){if(contains(sent,"completed"))saw_completed=1;if(contains(sent,"accepted")&&saw_completed)downgrade_errors++;}return send_error;}
static uint64_t uptime(void *c){(void)c;return now;}
static bool snapshot(service_mobile_snapshot_t *s,void *c){(void)c;if(lock_depth)lock_errors++;*s=sample;return !snapshot_unknown;}
static err_t submit(const service_mobile_request_t *r,void *c){(void)c;if(lock_depth||send_depth)lock_errors++;submits++;submitted=*r;if(sync_complete)service_mobile_complete(r->token,SERVICE_MOBILE_OK,SERVICE_MOBILE_MEASURING,2);if(race_complete)race_arm=1;return submit_error;}
static void extract_session(void){const char *p=sent;const char *key="\"session_id\":\"";while(*p&&!contains(p,key))p++;for(;*p;p++)if(memcmp(p,key,strlen(key))==0){p+=strlen(key);unsigned i=0;while(*p&&*p!='"')session[i++]=*p++;session[i]=0;return;}}
static void request(int id,const char *action){char b[256];snprintf(b,sizeof(b),"{\"v\":1,\"type\":\"command\",\"session_id\":\"%s\",\"req_id\":%u,\"action\":\"%s\"}",session,(unsigned)id,action);callbacks.on_text(7,b,strlen(b),callbacks.ctx);}
static void raw(const char *b){callbacks.on_text(7,b,strlen(b),callbacks.ctx);}
#define CHECK(c) do {if(!(c))return __LINE__;}while(0)
int run_tests(void)
{
    service_mobile_config_t cfg={.device_id="NIAIO-1234",.ap_ssid="NIAIO",.ap_password="",.ap_channel=1,.ap_max_connections=1,.submit=submit,.snapshot=snapshot,.monotonic_ms=uptime};
    sample.seq=10;sample.sample_uptime_ms=1000;sample.total_ml=125.5;sample.rate_mlh=60;sample.alert='A';sample.battery_percent=-1;memcpy(sample.clock_hms,"18:30:25",9);
    CHECK(service_mobile_start(&cfg)==ERR_OK);
    CHECK(service_mobile_start(&cfg)==ERR_BUSY);
    callbacks.on_open(7,callbacks.ctx);CHECK(contains(sent,"\"type\":\"hello\""));extract_session();CHECK(strlen(session)>0);
    request(1,"start");CHECK(submits==1);CHECK(submitted.command==SERVICE_MOBILE_START);CHECK(submitted.expires_uptime_ms==6000);CHECK(contains(sent,"\"phase\":\"accepted\""));
    uint64_t old_token=submitted.token;
    request(1,"start");CHECK(submits==1);CHECK(contains(sent,"accepted"));
    request(1,"pause");CHECK(submits==1);CHECK(contains(sent,"INVALID_ARGUMENT"));
    service_mobile_complete(old_token,SERVICE_MOBILE_OK,SERVICE_MOBILE_MEASURING,1);CHECK(contains(sent,"completed"));
    int terminal_sends=sends;service_mobile_complete(old_token,SERVICE_MOBILE_INTERNAL_ERROR,SERVICE_MOBILE_IDLE,0);CHECK(sends==terminal_sends);
    request(1,"start");CHECK(submits==1);CHECK(contains(sent,"completed"));
    raw("{\"v\":1,\"type\":\"command\",\"req_id\":1.0}");CHECK(contains(sent,"\"type\":\"error\""));
    raw("{\"v\":1,\"req_id\":2,\"req_id\":3}");CHECK(contains(sent,"\"type\":\"error\""));
    raw("{\"v\":1,\"req_id\":2,\"unknown\":0}");CHECK(contains(sent,"rejected"));
    raw("{\"v\":1,\"req_id\":2,}");CHECK(contains(sent,"\"type\":\"error\""));
    raw("{\"v\":1,\"req_id\":2}garbage");CHECK(contains(sent,"\"type\":\"error\""));
    raw("{\"v\":1,\"req_id\":2147483648}");CHECK(contains(sent,"\"type\":\"error\""));
    raw("{\"v\":1,\"req_id\":0}");CHECK(contains(sent,"\"type\":\"error\""));
    raw("{\"v\":1,\"req_id\":-1}");CHECK(contains(sent,"\"type\":\"error\""));
    raw("{\"v\":1,\"req_id\":01}");CHECK(contains(sent,"\"type\":\"error\""));
    raw("{\"v\":1,\"req_id\":true}");CHECK(contains(sent,"\"type\":\"error\""));
    raw("{\"v\":1,\"req_id\":\"2\"}");CHECK(contains(sent,"\"type\":\"error\""));
    raw("{\"v\":1,\"req_id\":20,\"r\\u0065q_id\":21}");CHECK(contains(sent,"\"type\":\"error\""));
    char b[300];snprintf(b,sizeof(b),"{\"v\":1,\"type\":\"time_sync\",\"session_id\":\"%s\",\"req_id\":10,\"hour\":23,\"minute\":59,\"second\":59}",session);
    raw(b);CHECK(submits==2);CHECK(submitted.command==SERVICE_MOBILE_TIME_SYNC);CHECK(submitted.hour==23);CHECK(submitted.minute==59);
    service_mobile_complete(submitted.token,SERVICE_MOBILE_OK,SERVICE_MOBILE_IDLE,1);raw(b);CHECK(submits==2);
    snprintf(b,sizeof(b),"{\"v\":1,\"type\":\"time_sync\",\"session_id\":\"%s\",\"req_id\":3,\"hour\":24,\"minute\":0,\"second\":0}",session);raw(b);CHECK(submits==2);CHECK(contains(sent,"INVALID_ARGUMENT"));
    submit_error=ERR_BUSY;request(4,"reset");CHECK(contains(sent,"rejected"));CHECK(contains(sent,"BUSY"));submit_error=0;
    int cached_count=submits;request(4,"reset");CHECK(submits==cached_count);CHECK(contains(sent,"BUSY"));
    submit_error=ERR_NOT_READY;request(6,"reset");CHECK(contains(sent,"NOT_READY"));
    submit_error=ERR_PARAM;request(7,"reset");CHECK(contains(sent,"INVALID_ARGUMENT"));
    submit_error=ERR_FAIL;request(8,"reset");CHECK(contains(sent,"INTERNAL_ERROR"));submit_error=0;
    request(9,"toggle");CHECK(contains(sent,"INVALID_ARGUMENT"));
    sync_complete=1;request(5,"start");CHECK(contains(sent,"completed"));request(5,"start");CHECK(contains(sent,"completed"));sync_complete=0;
    service_mobile_tick();CHECK(contains(sent,"status"));CHECK(contains(sent,"\"seq\":10"));CHECK(contains(sent,"\"battery_percent\":null"));CHECK(contains(sent,"125.5"));
    int before=sends;now=1500;service_mobile_tick();CHECK(sends==before);now=2000;service_mobile_tick();CHECK(sends==before+1);CHECK(contains(sent,"\"seq\":10"));CHECK(contains(sent,"\"sample_uptime_ms\":1000"));
    sample.state_seq=2;sample.seq=11;now=2020;service_mobile_tick();CHECK(contains(sent,"\"seq\":11"));
    before=sends;snapshot_unknown=1;now=3020;service_mobile_tick();CHECK(sends==before);snapshot_unknown=0;
    sample.rate_mlh=__builtin_nan("");service_mobile_tick();CHECK(sends==before);sample.rate_mlh=60;
    sample.clock_hms[0]='9';service_mobile_tick();CHECK(sends==before);sample.clock_hms[0]='1';
    sample.storage_state=(service_mobile_storage_t)-1;service_mobile_tick();CHECK(sends==before);sample.storage_state=SERVICE_MOBILE_STORAGE_ERROR;
    sample.battery_percent=100;sample.storage_checked_known=true;sample.storage_checked_uptime_ms=3000;
    sample.elapsed_s=90000;sample.total_drops=5000000000ULL;send_error=ERR_BUSY;service_mobile_tick();CHECK(sends==before+1);send_error=0;service_mobile_tick();CHECK(sends==before+2);
    CHECK(contains(sent,"\"elapsed_s\":90000"));CHECK(contains(sent,"\"total_drops\":5000000000"));CHECK(contains(sent,"\"storage_state\":\"ERROR\""));CHECK(contains(sent,"\"storage_checked_uptime_ms\":3000"));
    callbacks.on_close(7,callbacks.ctx);callbacks.on_open(8,callbacks.ctx);CHECK(contains(sent,"hello"));int n=sends;service_mobile_complete(old_token,SERVICE_MOBILE_OK,SERVICE_MOBILE_IDLE,0);CHECK(sends==n);
    /* Delayed text callback from the old generation must have no effect. */
    request(6,"start");CHECK(sends==n);
    callbacks.on_close(8,callbacks.ctx);callbacks.on_open(7,callbacks.ctx);extract_session();
    snprintf(b,sizeof(b),"{\"v\":1,\"type\":\"command\",\"session_id\":\"old-session\",\"req_id\":22,\"action\":\"start\"}");
    cached_count=submits;raw(b);CHECK(submits==cached_count);CHECK(contains(sent,"INVALID_ARGUMENT"));
    request(22,"start");CHECK(submits==cached_count+1);cached_count=submits;
    snprintf(b,sizeof(b),"{\"action\":\"start\",\"req_id\":23,\"session_id\":\"%s\",\"type\":\"command\",\"v\":1}",session);
    raw(b);CHECK(submits==cached_count+1);request(23,"start");CHECK(submits==cached_count+1);
    service_mobile_complete(submitted.token,SERVICE_MOBILE_EXPIRED,SERVICE_MOBILE_IDLE,3);CHECK(contains(sent,"completed"));CHECK(contains(sent,"EXPIRED"));
    for(int i=100;i<133;i++){request(i,"start");service_mobile_complete(submitted.token,SERVICE_MOBILE_OK,SERVICE_MOBILE_IDLE,3);}
    int count=submits;request(100,"start");CHECK(submits==count+1); /* 32-entry eviction */
    /* A malformed provider snapshot must never index an enum table. */
    sample.state=(service_mobile_state_t)-1;request(200,"pause");
    CHECK(contains(sent,"\"state\":\"IDLE\""));sample.state=SERVICE_MOBILE_IDLE;
    race_complete=1;request(201,"reset");CHECK(saw_completed);CHECK(downgrade_errors==0);CHECK(contains(sent,"completed"));race_complete=0;
    callbacks.on_close(7,callbacks.ctx);hello_queued=0;send_error=ERR_BUSY;callbacks.on_open(9,callbacks.ctx);
    CHECK(contains(sent,"hello"));service_mobile_tick();CHECK(contains(sent,"hello"));
    send_error=0;service_mobile_tick();CHECK(hello_queued);CHECK(hello_order_errors==0);CHECK(contains(sent,"status"));
    callbacks.on_close(9,callbacks.ctx);hello_queued=0;open_tick_arm=1;callbacks.on_open(10,callbacks.ctx);
    CHECK(hello_queued);CHECK(hello_order_errors==0);
    CHECK(lock_errors==0);service_mobile_stop();
    int ap_stops=wifi_stops, ws_stops=link_stops;
    service_mobile_stop();CHECK(wifi_stops==ap_stops);CHECK(link_stops==ws_stops);
    wifi_error=ERR_BUSY;CHECK(service_mobile_start(&cfg)==ERR_BUSY);service_mobile_stop();
    CHECK(wifi_stops==ap_stops);CHECK(link_stops==ws_stops);wifi_error=0;
    link_error=ERR_BUSY;CHECK(service_mobile_start(&cfg)==ERR_BUSY);CHECK(wifi_stops==ap_stops+1);
    service_mobile_stop();CHECK(link_stops==ws_stops);CHECK(wifi_stops==ap_stops+1);link_error=0;
    CHECK(service_mobile_start(&cfg)==ERR_OK);service_mobile_stop();return 0;
}
