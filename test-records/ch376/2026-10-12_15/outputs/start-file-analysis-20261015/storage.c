#include "service/storage/storage.h"
#include "freertos/FreeRTOS.h"
#include <stdarg.h>
static bool ready_ok=true,mount_ok=true,verify_ok=true,close_ok=true,write_ok=true;
static unsigned verify_calls,write_calls,create_calls;
static uint32_t expected_size,expected_offset;
static uint16_t expected_length;
static uint8_t open_code=0x14;
void *memcpy(void*d,const void*s,size_t n){char*a=d;const char*b=s;while(n--)*a++=*b++;return d;}
size_t strlen(const char*s){size_t n=0;while(s[n])n++;return n;}
int snprintf(char*b,size_t n,const char*f,...){(void)f;if(n>1){b[0]='U';b[1]=0;}return 1;}
void test_log(const char*t,const char*f,...){(void)t;(void)f;}
void test_lock(portMUX_TYPE*m){(void)m;}
void test_unlock(portMUX_TYPE*m){(void)m;}
uint64_t clock_ms(void){return 1234;}
void CH376_PORT_INIT(void){}
uint8_t CH376T_Get_IC_Ver(void){return 0x42;}
bool CH376T_SetUsbMode(void){return true;}
bool CH376T_CMD_DISK_CONNECT(void){return true;}
bool CH376T_DISK_MOUNT(void){return mount_ok;}
uint8_t CH376T_DiskCheckRaw(void){return 0x14;}
uint8_t CH376T_FILE_OPEN_STATUS(const char*s){(void)s;return open_code;}
bool CH376T_FILE_OPEN_NAME(const char*s){(void)s;return true;}
bool CH376T_FILE_CREATE_NAME(const char*s){(void)s;create_calls++;return true;}
bool CH376T_BYTE_WRITE_DATA(const uint8_t*b,uint16_t n){(void)b;(void)n;write_calls++;return write_ok;}
bool CH376T_FILE_CLOSE(void){return close_ok;}
uint32_t CH376T_GetFileSize(void){return strlen(STORAGE_HEADER);}
bool CH376T_BYTE_LOCATE(uint32_t n){(void)n;return true;}
void CH376_DiagReset(void){}
bool CH376_DiagPrepare(bool force){(void)force;return true;}
bool CH376_DiagEnsureReady(void){return ready_ok;}
bool CH376_DiagVerifyFile(const char*name,uint32_t size,uint32_t offset,const char*text,uint16_t length){(void)name;(void)text;verify_calls++;expected_size=size;expected_offset=offset;expected_length=length;return verify_ok;}
#ifdef BASELINE
#include "../../../../outputs/third-compat-validation-20261014/before/main/service/storage/storage.c"
#else
#include "service/storage/storage.c"
#endif
#define CHECK(x) do{if(!(x))return __LINE__;}while(0)
int test_minute_close_requires_readback(void){CHECK(storage_init()==ERR_OK);CHECK(storage_open_session("U1")==ERR_OK);CHECK(storage_write_open("row")==ERR_OK);CHECK(storage_close_session()==ERR_OK);CHECK(verify_calls==1&&expected_size==strlen(STORAGE_HEADER)+3&&expected_offset==strlen(STORAGE_HEADER)&&expected_length==3);return 0;}
int test_readback_failure_reports_error(void){CHECK(storage_init()==ERR_OK);CHECK(storage_open_session("U1")==ERR_OK);CHECK(storage_write_open("row")==ERR_OK);verify_ok=false;CHECK(storage_close_session()==ERR_FAIL);storage_status_t s;CHECK(storage_get_status(&s)&&s.state==STORAGE_ERROR);return 0;}
int test_close_failure_skips_verification(void){CHECK(storage_init()==ERR_OK);CHECK(storage_open_session("U1")==ERR_OK);CHECK(storage_write_open("row")==ERR_OK);close_ok=false;CHECK(storage_close_session()==ERR_FAIL);CHECK(verify_calls==0);return 0;}
int test_compat_failure_stops_initialization(void){ready_ok=false;CHECK(storage_init()==ERR_FAIL);CHECK(write_calls==0);return 0;}
int test_mount_failure_stops_initialization(void){mount_ok=false;CHECK(storage_init()==ERR_FAIL);CHECK(write_calls==0);return 0;}
int test_no_write_no_readback(void){CHECK(storage_init()==ERR_OK);CHECK(storage_open_session("U1")==ERR_OK);CHECK(storage_close_session()==ERR_OK);CHECK(verify_calls==0);return 0;}
int test_multiple_appends_last_range(void){CHECK(storage_init()==ERR_OK);CHECK(storage_open_session("U1")==ERR_OK);CHECK(storage_write_open("one")==ERR_OK);CHECK(storage_write_open("two")==ERR_OK);CHECK(storage_close_session()==ERR_OK);CHECK(expected_size==strlen(STORAGE_HEADER)+6&&expected_offset==strlen(STORAGE_HEADER)+3);return 0;}
int test_new_header_requires_readback(void){char stem[9];CHECK(storage_init()==ERR_OK);open_code=0x42;CHECK(storage_create_session(stem,sizeof(stem))==ERR_OK);CHECK(verify_calls==1&&expected_offset==0&&expected_length==strlen(STORAGE_HEADER));return 0;}

int test_created_but_failed_validation_leaves_session_empty(void){
 char stem[9]={0};CHECK(storage_init()==ERR_OK);open_code=0x42;verify_ok=false;
 CHECK(storage_create_session(stem,sizeof(stem))==ERR_FAIL);
 CHECK(create_calls==1&&write_calls==1&&verify_calls==1&&stem[0]==0&&s_next_session==1);return 0;
}
int test_append_open_missing_automatically_recreates(void){
 CHECK(storage_init()==ERR_OK);open_code=0x42;
 CHECK(storage_open_session("U0000001")==ERR_OK);
 CHECK(create_calls==1&&write_calls==1&&s_file_open);return 0;
}
int test_ensure_open_missing_automatically_recreates(void){
 CHECK(storage_init()==ERR_OK);open_code=0x42;
 CHECK(storage_ensure_session("U0000001")==ERR_OK);
 CHECK(create_calls==1&&write_calls==1&&verify_calls==1);return 0;
}
