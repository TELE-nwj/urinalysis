#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define CH376_SPI_H
#define CMD_RD_USB_DATA0 0x27
#define CMD_DISK_READ 0x54
#define CMD_DISK_RD_GO 0x55
#define USB_INT_SUCCESS 0x14
#define USB_INT_DISK_READ 0x1d
#define CHECK(x) do { if (!(x)) return __LINE__; } while(0)
static unsigned commands[256], command_count, active_cmd, read_index, wait_count, go_count, bad_write;
static unsigned scenario, requested, read_count, var_address, param_count;
static unsigned mount_result=1, mount_count;
static uint8_t params[8];
void test_log(const char *tag,const char *format,...) {(void)tag;(void)format;}
void CH376_Cmd(uint8_t cmd) {
    active_cmd=cmd;read_index=0;param_count=0;
    if(command_count<256) commands[command_count++]=cmd;
    if(cmd==0x55) go_count++;
    if(cmd==0x0b||cmd==0x0d||cmd==0x38||cmd==0x56||cmd==0x57||cmd==0x4c||cmd==0x35) bad_write++;
}
void CH376_Cmd_End(void) {}
void CH376_Write(uint8_t v) { if(param_count<8)params[param_count++]=v; if(active_cmd==0x0c||active_cmd==0x0a)var_address=v; }
uint8_t CH376_Read(void) {
    read_count++;
    if(active_cmd==0x0c) return (uint8_t)(0x12345678u>>(8*read_index++));
    if(active_cmd==0x0a) return 8;
    if(active_cmd==0x27) {
        if(read_index++==0) return (uint8_t)requested;
        if(scenario==4)return 0xff;
        if(scenario==5) { const uint8_t b[4]={0x48,0x40,0,0}; return b[(read_index-2)%4]; }
        return (uint8_t)(read_index-2);
    }
    return 0;
}
uint8_t wait(void) {
    wait_count++;
    if(scenario==1 && wait_count==3)return 0x20;
    if(scenario==2||scenario==3)return 0x1d;
    return wait_count<=8?0x1d:0x14;
}
uint8_t CH376T_DISK_MOUNT(void) { mount_count++; return mount_result; }
size_t strlen(const char*s){size_t n=0;while(s[n])n++;return n;}
void *memset(void*p,int x,size_t n){uint8_t*b=p;while(n--)*b++=(uint8_t)x;return p;}
#include "../../main/driver/ch376/ch376_diag.c"
static void put16(uint8_t*b,unsigned o,uint16_t v){b[o]=v;b[o+1]=v>>8;}
static void put32(uint8_t*b,unsigned o,uint32_t v){for(unsigned i=0;i<4;i++)b[o+i]=v>>(i*8);}
static void valid_bpb(uint8_t*b){memset(b,0,512);put16(b,11,512);b[13]=8;put16(b,14,1056);b[16]=2;put32(b,32,7848192);put32(b,36,7664);put32(b,44,2);put16(b,48,1);put16(b,50,6);b[510]=0x55;b[511]=0xaa;}
int test_bpb_high_byte_and_absolute_lba(void){uint8_t b[512];valid_bpb(b);CHECK(diag_parse_bpb(b,64));CHECK(s_geometry.reserved==1056);CHECK(s_geometry.data_lba==16448);CHECK(s_geometry.fat2_lba==8784);return 0;}
int test_bpb_bad_signature(void){uint8_t b[512];valid_bpb(b);b[510]=0;CHECK(!diag_parse_bpb(b,64));return 0;}
int test_bpb_bad_cluster_size(void){uint8_t b[512];valid_bpb(b);b[13]=3;CHECK(!diag_parse_bpb(b,64));return 0;}
int test_bpb_overflow(void){uint8_t b[512];valid_bpb(b);put32(b,36,0xffffffffu);CHECK(!diag_parse_bpb(b,64));return 0;}
int test_sector_read_eight_chunks(void){uint8_t b[512];requested=64;CHECK(diag_read_sector(64,b));CHECK(wait_count==9&&go_count==8);CHECK(!bad_write);for(unsigned i=0;i<512;i++)CHECK(b[i]==i%64);CHECK(commands[0]==0x54);return 0;}
int test_sector_read_partial_failure(void){uint8_t b[512];requested=64;scenario=1;CHECK(!diag_read_sector(64,b));CHECK(wait_count==3);CHECK(!bad_write);return 0;}
int test_sector_zero_progress_bounded(void){uint8_t b[512];scenario=2;requested=0;CHECK(!diag_read_sector(64,b));CHECK(wait_count<=33&&go_count<=32);CHECK(!bad_write);return 0;}
int test_sector_oversize_consumed_rejected(void){uint8_t b[512];scenario=3;requested=255;CHECK(!diag_read_sector(64,b));CHECK(read_count==256);CHECK(!bad_write);return 0;}
int test_var32_little_endian(void){CHECK(diag_read32(0x50)==0x12345678u);CHECK(var_address==0x50&&command_count==1&&commands[0]==0x0c);CHECK(!bad_write);return 0;}
int test_locate_lba(void){scenario=5;requested=4;CHECK(CH376_DiagLocateResult(56)==0x4048);CHECK(commands[0]==0x27&&!bad_write);return 0;}
int test_locate_eof_is_valid(void){scenario=4;requested=4;CHECK(CH376_DiagLocateResult(56)==UINT32_MAX);CHECK(!bad_write);return 0;}
int test_locate_bad_length(void){requested=7;CHECK(CH376_DiagLocateResult(56)==UINT32_MAX);CHECK(read_count==8&&!bad_write);return 0;}
int test_probe_remount_failure_stops(void){requested=64;mount_result=0;CHECK(!CH376_DiagPrepare(true));CHECK(mount_count==1&&!s_geometry.valid&&!bad_write);return 0;}
int test_bad_boot_remount_no_fake_geometry(void){requested=64;CHECK(CH376_DiagPrepare(true));CHECK(mount_count==1&&!s_geometry.valid&&!bad_write);return 0;}
int test_cached_geometry_avoids_physical_read(void){uint8_t b[512];valid_bpb(b);CHECK(diag_parse_bpb(b,64));CHECK(CH376_DiagPrepare(false));CHECK(mount_count==0&&!bad_write);for(unsigned i=0;i<command_count;i++)CHECK(commands[i]!=0x54);return 0;}
int test_reset_invalidates_geometry(void){uint8_t b[512];valid_bpb(b);CHECK(diag_parse_bpb(b,64));CH376_DiagReset();CHECK(!s_geometry.valid);return 0;}
