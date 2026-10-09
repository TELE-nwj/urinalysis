#include "service/storage/storage.h"
#include "freertos/FreeRTOS.h"
#include <stdarg.h>
static unsigned io_count;
static int mode_ok=1,connect_ok=1,mount_ok=1,write_ok=1,close_ok=1;
static uint8_t disk_status=0x14;
static uint8_t open_status=0x14;
static uint64_t now_ms=1234;
void *memcpy(void*d,const void*s,size_t n){char*a=d;const char*b=s;while(n--)*a++=*b++;return d;}
size_t strlen(const char*s){size_t n=0;while(s[n])n++;return n;}
int snprintf(char*b,size_t n,const char*f,...){(void)f;if(n>1){b[0]='U';b[1]=0;}return 1;}
void test_log(const char*t,const char*f,...){(void)t;(void)f;}
void test_lock(portMUX_TYPE*m){(void)m;}
void test_unlock(portMUX_TYPE*m){(void)m;}
uint64_t clock_ms(void){return now_ms;}
void CH376_PORT_INIT(void){io_count++;}
uint8_t CH376T_Get_IC_Ver(void){io_count++;return 1;}
bool CH376T_SetUsbMode(void){io_count++;return mode_ok;}
bool CH376T_CMD_DISK_CONNECT(void){io_count++;return connect_ok;}
bool CH376T_DISK_MOUNT(void){io_count++;return mount_ok;}
uint8_t CH376T_DiskCheckRaw(void){io_count++;return disk_status;}
uint8_t CH376T_FILE_OPEN_STATUS(const char*s){(void)s;io_count++;return open_status;}
bool CH376T_FILE_OPEN_NAME(const char*s){(void)s;io_count++;return true;}
bool CH376T_FILE_CREATE_NAME(const char*s){(void)s;io_count++;return true;}
bool CH376T_BYTE_WRITE_DATA(const uint8_t*b,uint16_t n){(void)b;(void)n;io_count++;return write_ok;}
bool CH376T_FILE_CLOSE(void){io_count++;return close_ok;}
uint32_t CH376T_GetFileSize(void){io_count++;return 0;}
bool CH376T_BYTE_LOCATE(uint32_t n){(void)n;io_count++;return true;}
#ifdef RED_STORAGE
bool storage_get_status(storage_status_t*s){s->state=STORAGE_UNKNOWN;s->checked=false;s->checked_uptime_ms=0;return true;}
#endif
#ifdef RED_STORAGE
#include "build-host/baseline_storage.c"
#else
#include "service/storage/storage.c"
#endif
#define CHECK(x) do{if(!(x))return __LINE__;}while(0)
int test_storage_cache_observes_errors_and_absence(void){storage_status_t st;CHECK(storage_get_status(&st)&&st.state==STORAGE_UNKNOWN&&!st.checked);CHECK(io_count==0);mode_ok=0;CHECK(storage_init()==ERR_FAIL);CHECK(storage_get_status(&st)&&st.state==STORAGE_ERROR&&st.checked&&st.checked_uptime_ms==1234);mode_ok=1;CHECK(storage_init()==ERR_OK);CHECK(storage_get_status(&st)&&st.state==STORAGE_READY);unsigned count=io_count;for(unsigned i=0;i<50;i++)CHECK(storage_get_status(&st));CHECK(io_count==count);disk_status=0x16;now_ms=2000;CHECK(storage_ensure_session("U0000001")==ERR_NOT_READY);CHECK(storage_get_status(&st)&&st.state==STORAGE_ABSENT&&st.checked_uptime_ms==2000);return 0;}
int test_storage_io_failure_not_absent(void){storage_status_t st;CHECK(storage_init()==ERR_OK);CHECK(storage_open_session("U0000001")==ERR_OK);write_ok=0;CHECK(storage_write_open("row")==ERR_FAIL);CHECK(storage_get_status(&st)&&st.state==STORAGE_ERROR);close_ok=0;CHECK(storage_close_session()==ERR_FAIL);CHECK(storage_get_status(&st)&&st.state==STORAGE_ERROR);return 0;}
int test_mount_warning_remains_error_until_verified(void){storage_status_t st;mount_ok=0;CHECK(storage_init()==ERR_OK);CHECK(storage_get_status(&st)&&st.state==STORAGE_ERROR);mount_ok=1;CHECK(storage_ensure_session("U0000001")==ERR_OK);CHECK(storage_get_status(&st)&&st.state==STORAGE_READY);return 0;}
int test_reinserted_media_io_error_does_not_reuse_absence(void){storage_status_t st;CHECK(storage_init()==ERR_OK);disk_status=0x16;CHECK(storage_ensure_session("U0000001")==ERR_NOT_READY);CHECK(storage_get_status(&st)&&st.state==STORAGE_ABSENT);disk_status=0x14;CHECK(storage_init()==ERR_OK);disk_status=0x16;CHECK(storage_ensure_session("U0000001")==ERR_NOT_READY);disk_status=0x14;/* Recovery initializes media, then the current file operation fails. */open_status=0x1f;now_ms=9000;CHECK(storage_ensure_session("U0000001")==ERR_NOT_READY);CHECK(storage_get_status(&st)&&st.state==STORAGE_ERROR&&st.checked_uptime_ms==9000);return 0;}
int test_not_ready_without_current_disconnect_is_not_absent(void){storage_status_t st;CHECK(storage_init()==ERR_OK);disk_status=0x16;CHECK(storage_ensure_session("U0000001")==ERR_NOT_READY);CHECK(storage_get_status(&st)&&st.state==STORAGE_ABSENT);now_ms=8000;CHECK(storage_open_session("U0000001")==ERR_NOT_READY);CHECK(storage_get_status(&st)&&st.state==STORAGE_ERROR);return 0;}
