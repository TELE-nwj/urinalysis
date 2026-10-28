/* 文件系统只存在内存中；联合执行真实状态机、计时与存储服务。 */
#include "driver/ch376/ch376_spi.h"
#include "driver/ch376/ch376_diag.h"
typedef struct { char name[40]; char data[512]; uint32_t size; } model_file_t;
static model_file_t files[8];
static unsigned file_count, create_count, byte_write_count, verify_failures, missing_opens;
static int current_file = -1;
static uint32_t cursor;
static bool disk_present = true, model_close_ok = true, header_write_ok = true;
static bool create_reply_ok = true;
static uint8_t forced_open_code;

/* 测试只需要文件名、字符串复制；不替代真实库的尿量数值格式化验收。 */
int snprintf(char *out,size_t n,const char *fmt,...)
{
    char tmp[160];size_t used=0;va_list ap;va_start(ap,fmt);
    if(!strcmp(fmt,"U%07lu")){
        unsigned long id=va_arg(ap,unsigned long);tmp[used++]='U';
        unsigned long div=1000000;while(div){tmp[used++]=(char)('0'+id/div%10);div/=10;}
    }else if(!strcmp(fmt,"%s")||!strcmp(fmt,"/%s.TXT")){
        const char*s=va_arg(ap,const char*);if(fmt[0]=='/')tmp[used++]='/';
        while(*s&&used<sizeof(tmp)-5)tmp[used++]=*s++;
        if(fmt[0]=='/'){tmp[used++]='.';tmp[used++]='T';tmp[used++]='X';tmp[used++]='T';}
    }
    va_end(ap);if(n){size_t k=used<n-1?used:n-1;memcpy(out,tmp,k);out[k]=0;}return (int)used;
}
static int find_file(const char*name){for(unsigned i=0;i<file_count;i++)if(!strcmp(files[i].name,name))return (int)i;return -1;}
static int add_file(const char*name,const char*data){int i=(int)file_count++;snprintf(files[i].name,sizeof(files[i].name),"%s",name);files[i].size=strlen(data);memcpy(files[i].data,data,files[i].size+1);return i;}
static bool same_bytes(const char*a,const char*b,size_t n){while(n--)if(*a++!=*b++)return false;return true;}
void CH376_PORT_INIT(void){}
uint8_t CH376T_Get_IC_Ver(void){return 0x42;}
bool CH376T_SetUsbMode(void){return true;}
bool CH376T_CMD_DISK_CONNECT(void){return disk_present;}
bool CH376T_DISK_MOUNT(void){current_file=-1;return disk_present;}
uint8_t CH376T_DiskCheckRaw(void){return disk_present?0x14:0x16;}
uint8_t CH376T_FILE_OPEN_STATUS(const char*name){
    if(forced_open_code){uint8_t code=forced_open_code;forced_open_code=0;current_file=-1;return code;}
    if(missing_opens){missing_opens--;current_file=-1;return 0x42;}
    current_file=find_file(name);cursor=0;return current_file>=0?0x14:0x42;
}
bool CH376T_FILE_OPEN_NAME(const char*name){return CH376T_FILE_OPEN_STATUS(name)==0x14;}
bool CH376T_FILE_CREATE_NAME(const char*name){
    create_count++;current_file=find_file(name);if(current_file<0)current_file=add_file(name,"");
    files[current_file].size=0;files[current_file].data[0]=0;cursor=0;return create_reply_ok;
}
bool CH376T_BYTE_WRITE_DATA(const uint8_t*data,uint16_t length){
    byte_write_count++;if(current_file<0||!header_write_ok||cursor+length>=sizeof(files[0].data))return false;
    model_file_t*f=&files[current_file];memcpy(f->data+cursor,data,length);cursor+=length;
    if(cursor>f->size)f->size=cursor;
    f->data[f->size]=0;return true;
}
bool CH376T_FILE_CLOSE(void){current_file=-1;return model_close_ok;}
uint32_t CH376T_GetFileSize(void){return current_file<0?UINT32_MAX:files[current_file].size;}
bool CH376T_BYTE_LOCATE(uint32_t offset){if(current_file<0||offset>files[current_file].size)return false;cursor=offset;return true;}
void CH376_DiagReset(void){}
bool CH376_DiagPrepare(bool force){(void)force;return disk_present;}
bool CH376_DiagEnsureReady(void){return disk_present;}
bool CH376_DiagVerifyFile(const char*name,uint32_t size,uint32_t offset,const char*text,uint16_t length){
    if(verify_failures){verify_failures--;return false;}int i=find_file(name);
    return i>=0&&files[i].size==size&&offset+length<=size&&same_bytes(files[i].data+offset,text,length);
}
#define TAG STORAGE_TAG
#include "service/storage/storage.c"
#undef TAG

int test_session_normal_resume_duplicate_start_and_reset(void){
    CHECK(app_monitor_start()==ERR_OK);key_cb(INPUT_KEY1);CHECK(create_count==1);
    CHECK(!strcmp(S1_session_file(),"U0000001"));
    CHECK(submit(APP_COMMAND_START,1,5000)==ERR_OK);monitor_step();CHECK(create_count==1);
    for(unsigned i=0;i<3;i++){key_cb(INPUT_KEY1);now_ms+=10000;monitor_step();key_cb(INPUT_KEY1);monitor_step();CHECK(create_count==1&&app_monitor_state()==APP_MEASURING);}
    key_cb(INPUT_KEY2);key_cb(INPUT_KEY1);CHECK(create_count==2&&file_count==2);
    CHECK(!strcmp(S1_session_file(),"U0000002"));return 0;
}
int test_session_header_verification_failure_keeps_reserved_name(void){
    CHECK(app_monitor_start()==ERR_OK);verify_failures=1;key_cb(INPUT_KEY1);
    CHECK(!s_storage_ok&&create_count==1&&file_count==1);
    CHECK(!strcmp(S1_session_file(),"U0000001"));
    key_cb(INPUT_KEY1);now_ms=10000;monitor_step();CHECK(create_count==1);
    key_cb(INPUT_KEY1);monitor_step();CHECK(s_storage_ok&&create_count==1&&file_count==1);
    CHECK(!strcmp(S1_session_file(),"U0000001"));return 0;
}
int test_session_running_retry_does_not_allocate_second_name(void){
    CHECK(app_monitor_start()==ERR_OK);verify_failures=1;key_cb(INPUT_KEY1);
    now_ms=6000;monitor_step();CHECK(s_storage_ok&&create_count==1&&file_count==1);return 0;
}
int test_session_create_reply_failure_does_not_resend_create(void){
    CHECK(app_monitor_start()==ERR_OK);create_reply_ok=false;key_cb(INPUT_KEY1);
    CHECK(create_count==1&&!strcmp(S1_session_file(),"U0000001"));
    create_reply_ok=true;for(unsigned i=1;i<=3;i++){now_ms=i*6000;monitor_step();}
    CHECK(!s_storage_ok&&create_count==1&&file_count==1);return 0;
}
int test_session_partial_header_is_not_rewritten(void){
    CHECK(app_monitor_start()==ERR_OK);header_write_ok=false;key_cb(INPUT_KEY1);
    CHECK(create_count==1&&!strcmp(S1_session_file(),"U0000001"));
    header_write_ok=true;now_ms=6000;monitor_step();
    CHECK(!s_storage_ok&&create_count==1&&byte_write_count==1&&files[0].size==0);return 0;
}
int test_session_close_reply_failure_can_recover_same_file(void){
    CHECK(app_monitor_start()==ERR_OK);model_close_ok=false;key_cb(INPUT_KEY1);
    CHECK(!s_storage_ok&&!strcmp(S1_session_file(),"U0000001"));
    model_close_ok=true;now_ms=6000;monitor_step();CHECK(s_storage_ok&&create_count==1&&file_count==1);return 0;
}
int test_session_append_missing_does_not_overwrite_previous_rows(void){
    char stem[9]={0};CHECK(storage_create_session(stem,sizeof(stem))==ERR_OK);
    CHECK(storage_open_session(stem)==ERR_OK);CHECK(storage_write_open("minute one\r\n")==ERR_OK);CHECK(storage_close_session()==ERR_OK);
    uint32_t old_size=files[0].size;unsigned old_writes=byte_write_count;missing_opens=1;
    CHECK(storage_open_session(stem)!=ERR_OK);CHECK(storage_write_open("bad")==ERR_NOT_READY);
    CHECK(create_count==1&&files[0].size==old_size&&byte_write_count==old_writes);
    CHECK(storage_ensure_session(stem)==ERR_OK);CHECK(storage_open_session(stem)==ERR_OK);
    CHECK(storage_write_open("minute two\r\n")==ERR_OK);CHECK(storage_close_session()==ERR_OK);
    CHECK(create_count==1&&same_bytes(files[0].data+strlen(STORAGE_HEADER),"minute one\r\nminute two\r\n",24));return 0;
}
int test_session_recovery_missing_does_not_create(void){
    char stem[9]={0};CHECK(storage_create_session(stem,sizeof(stem))==ERR_OK);uint32_t size=files[0].size;
    missing_opens=1;CHECK(storage_ensure_session(stem)!=ERR_OK);
    CHECK(create_count==1&&files[0].size==size);CHECK(storage_ensure_session(stem)==ERR_OK);return 0;
}
int test_session_new_disk_missing_known_file_does_not_create(void){
    char stem[9]={0};CHECK(storage_create_session(stem,sizeof(stem))==ERR_OK);
    file_count=0;CHECK(storage_ensure_session(stem)!=ERR_OK);CHECK(create_count==1&&file_count==0);return 0;
}
int test_session_no_disk_then_insert_creates_only_once(void){
    CHECK(app_monitor_start()==ERR_OK);disk_present=false;key_cb(INPUT_KEY1);
    CHECK(!s_storage_ok&&create_count==0&&S1_session_file()[0]==0);
    disk_present=true;now_ms=6000;monitor_step();CHECK(s_storage_ok&&create_count==1&&file_count==1);return 0;
}
int test_session_existing_records_are_skipped_and_preserved(void){
    add_file("/U0000001.TXT","historical data");CHECK(app_monitor_start()==ERR_OK);key_cb(INPUT_KEY1);
    CHECK(!strcmp(S1_session_file(),"U0000002")&&create_count==1&&!strcmp(files[0].data,"historical data"));
    key_cb(INPUT_KEY2);key_cb(INPUT_KEY1);CHECK(!strcmp(S1_session_file(),"U0000003")&&create_count==2);return 0;
}
int test_session_repeated_create_api_uses_existing_name(void){
    char stem[9]={0};CHECK(storage_create_session(stem,sizeof(stem))==ERR_OK);
    CHECK(storage_create_session(stem,sizeof(stem))==ERR_OK);
    CHECK(!strcmp(stem,"U0000001")&&create_count==1&&file_count==1);return 0;
}
int test_session_suspicious_short_length_blocks_append(void){
    char stem[9]={0};CHECK(storage_create_session(stem,sizeof(stem))==ERR_OK);files[0].size=0;
    unsigned old_writes=byte_write_count;CHECK(storage_open_session(stem)!=ERR_OK);
    CHECK(storage_write_open("bad")==ERR_NOT_READY&&byte_write_count==old_writes&&create_count==1);return 0;
}
int test_session_failed_lookup_does_not_reserve_or_create(void){
    char stem[9]={0};CHECK(storage_init()==ERR_OK);forced_open_code=0x1F;
    CHECK(storage_create_session(stem,sizeof(stem))!=ERR_OK);CHECK(!stem[0]&&create_count==0);return 0;
}
