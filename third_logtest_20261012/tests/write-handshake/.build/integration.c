#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define CH376_SPI_H
#define CMD_RD_USB_DATA0 0x27
#define CMD_DISK_READ 0x54
#define CMD_DISK_RD_GO 0x55
#define CMD_SET_FILE_NAME 0x2f
#define CMD_FILE_OPEN 0x32
#define CMD_FILE_CLOSE 0x36
#define CMD_BYTE_READ 0x3a
#define CMD_BYTE_RD_GO 0x3b
#define USB_INT_SUCCESS 0x14
#define USB_INT_DISK_READ 0x1d
#define CHECK(x) do { if (!(x)) return __LINE__; } while(0)
static unsigned commands[256], command_count, active_cmd, read_index, wait_count, go_count, bad_write;
static unsigned scenario, requested, read_count, var_address, param_count;
static unsigned mount_result=1, mount_count;
static uint8_t params[8];
static uint32_t regs[256];
static uint8_t bytes8[256];
static unsigned writes32, write_addr;
static uint32_t write_value;
static unsigned open_result=0x14, close_result=0x14, locate_result=1;
static uint8_t payload[96];
static uint8_t fat_model[2][512];
static uint32_t read_lba;
static unsigned disk_chunk;
static bool fat_read_error;
static bool physical_stream;
/* 1E交接阶段的有状态模型：内部变量窗口不能作为稳定文件状态读取。 */
static bool protocol_pending;
static unsigned protocol_bad_commands, protocol_goal, protocol_written, protocol_chunk=3;
static uint8_t protocol_payload[128];
void test_log(const char *tag,const char *format,...) {(void)tag;(void)format;}
void CH376_Cmd(uint8_t cmd) {
    if(scenario==30&&protocol_pending&&cmd!=0x2d&&cmd!=0x3d)protocol_bad_commands++;
    active_cmd=cmd;read_index=0;param_count=0;
    if(command_count<256) commands[command_count++]=cmd;
    if(cmd==0x55) go_count++;
    if(scenario==20&&cmd==0x54){disk_chunk=0;physical_stream=true;}
    if(cmd==0x3a||cmd==0x36)physical_stream=false;
    if(scenario==20&&cmd==0x55)disk_chunk++;
    if(cmd==0x0b||cmd==0x0d||cmd==0x38||cmd==0x56||cmd==0x57||cmd==0x4c||cmd==0x35) bad_write++;
}
void CH376_Cmd_End(void) {}
void CH376_Write(uint8_t v) {
    if(scenario==30&&active_cmd==0x2d&&protocol_written<sizeof(protocol_payload))protocol_payload[protocol_written++]=v;
    if(param_count<8)params[param_count++]=v;
    if(active_cmd==0x0c||active_cmd==0x0a)var_address=v;
    if(scenario==20&&active_cmd==0x54&&param_count==5)
        read_lba=(uint32_t)params[0]|((uint32_t)params[1]<<8)|((uint32_t)params[2]<<16)|((uint32_t)params[3]<<24);
    if(active_cmd==0x0d) {
        if(param_count==1){write_addr=v;write_value=0;}
        else if(param_count<=5)write_value|=(uint32_t)v<<(8*(param_count-2));
        if(param_count==5){regs[write_addr]=write_value;writes32++;}
    }
}
uint8_t CH376_Read(void) {
    read_count++;
    if(scenario==30&&protocol_pending&&active_cmd==0x0c){const uint8_t unstable[4]={'F','I','L','E'};return unstable[read_index++%4];}
    if(scenario==30&&active_cmd==0x2d){unsigned left=protocol_goal-protocol_written;return left<protocol_chunk?left:protocol_chunk;}
    if(scenario==20&&physical_stream&&active_cmd==0x27){
        if(read_index++==0)return 64;
        unsigned at=disk_chunk*64+read_index-2;
        if(at>=512)return 0;
        return fat_model[read_lba==8784?1:0][at];
    }
    if(scenario>=10){
        if(active_cmd==0x01)return bytes8[1];
        if(active_cmd==0x0a)return bytes8[var_address];
        if(active_cmd==0x0c)return (uint8_t)(regs[var_address]>>(8*read_index++));
        if(active_cmd==0x27){if(read_index++==0)return requested;return payload[read_index-2];}
    }
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
    if(scenario==30&&active_cmd==0x3c){protocol_pending=true;return 0x1e;}
    if(scenario==30&&active_cmd==0x3d){
        if(protocol_written<protocol_goal)return 0x1e;
        protocol_pending=false;regs[0x6c]+=protocol_written;
        if(regs[0x6c]>regs[0x68])regs[0x68]=regs[0x6c];return 0x14;
    }
    if(scenario==20&&(active_cmd==0x54||active_cmd==0x55))return fat_read_error?0x20:(disk_chunk<8?0x1d:0x14);
    if(scenario>=10){
        if(active_cmd==0x32){bytes8[0x2b]=0x12;return open_result;}
        if(active_cmd==0x36)return close_result;
        if(active_cmd==0x3a)return 0x1d;
        if(active_cmd==0x3b)return 0x14;
    }
    if(scenario==1 && wait_count==3)return 0x20;
    if(scenario==2||scenario==3)return 0x1d;
    return wait_count<=8?0x1d:0x14;
}
uint8_t CH376T_DISK_MOUNT(void) { mount_count++; return mount_result; }
uint8_t CH376T_FILE_OPEN_STATUS(const char *name){(void)name;return open_result;}
uint32_t CH376T_GetFileSize(void){return regs[0x68];}
uint8_t CH376T_BYTE_LOCATE(uint32_t offset){regs[0x6c]=offset;return locate_result;}
size_t strlen(const char*s){size_t n=0;while(s[n])n++;return n;}
void *memset(void*p,int x,size_t n){uint8_t*b=p;while(n--)*b++=(uint8_t)x;return p;}
#include "F:/ESP32/niaio/git/third_logtest_20261012/main/driver/ch376/ch376_diag.c"
static void put16(uint8_t*b,unsigned o,uint16_t v){b[o]=v;b[o+1]=v>>8;}
static void put32(uint8_t*b,unsigned o,uint32_t v){for(unsigned i=0;i<4;i++)b[o+i]=v>>(i*8);}
static void valid_bpb(uint8_t*b){memset(b,0,512);put16(b,11,512);b[13]=8;b[21]=0xf8;put16(b,14,1056);b[16]=2;put32(b,32,7848192);put32(b,36,7664);put32(b,44,2);put16(b,48,1);put16(b,50,6);b[510]=0x55;b[511]=0xaa;}
static void valid_fats(void){for(unsigned i=0;i<2;i++){memset(fat_model[i],0,512);put32(fat_model[i],0,0x0ffffff8);put32(fat_model[i],4,0x0fffffff);put32(fat_model[i],8,0x0fffffff);}}
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
int test_cached_geometry_still_checks_fat(void){uint8_t b[512];valid_bpb(b);CHECK(diag_parse_bpb(b,64));scenario=20;valid_fats();CHECK(CH376_DiagPrepare(false));CHECK(mount_count==1&&!bad_write);unsigned reads=0;for(unsigned i=0;i<command_count;i++)reads+=commands[i]==0x54;CHECK(reads==2);return 0;}
int test_reset_invalidates_geometry(void){uint8_t b[512];valid_bpb(b);CHECK(diag_parse_bpb(b,64));CH376_DiagReset();CHECK(!s_geometry.valid);return 0;}
static void legacy_setup(void){uint8_t b[512];valid_bpb(b);diag_parse_bpb(b,64);scenario=20;valid_fats();bytes8[1]=0x42;bytes8[0x2b]=3;bytes8[0xcf]=4;bytes8[0x38]=8;regs[0x44]=2;regs[0x4c]=64;regs[0x50]=15424;regs[0x70]=99;CH376_DiagPrepare(false);}
int test_legacy_correction_and_no_repeat(void){legacy_setup();CHECK(CH376_DiagEnsureReady());CHECK(regs[0x4c]==1088&&regs[0x50]==16448&&regs[0x70]==0&&writes32==3);CHECK(CH376_DiagEnsureReady());CHECK(writes32==3);return 0;}
int test_legacy_high_byte_mismatch_blocks(void){legacy_setup();bytes8[0xcf]=3;CHECK(!CH376_DiagEnsureReady());CHECK(!writes32);return 0;}
int test_legacy_ready_wrong_base_blocks(void){legacy_setup();bytes8[0x2b]=0x10;CHECK(!CH376_DiagEnsureReady());CHECK(!writes32);return 0;}
int test_new_chip_no_legacy_writes(void){legacy_setup();bytes8[1]=0x43;regs[0x50]=16448;CHECK(CH376_DiagEnsureReady());CHECK(!writes32);return 0;}
int test_legacy_root_open_failure_blocks(void){legacy_setup();open_result=0x20;CHECK(!CH376_DiagEnsureReady());CHECK(!writes32);return 0;}
int test_bad_data_base_blocks_write(void){legacy_setup();regs[0x5c]=16448;CHECK(!CH376_DiagCanWrite("test"));return 0;}
int test_directory_in_fat_blocks_write(void){legacy_setup();regs[0x50]=16448;regs[0x5c]=15424;CHECK(!CH376_DiagCanWrite("test"));return 0;}
int test_valid_directory_allows_write(void){legacy_setup();regs[0x50]=16448;regs[0x5c]=16448;CHECK(CH376_DiagCanWrite("test"));return 0;}
int test_readback_bytes_match(void){scenario=10;requested=3;payload[0]=1;payload[1]=2;payload[2]=3;uint8_t expected[3]={1,2,3};CHECK(diag_compare_bytes(expected,3));CHECK(wait_count==2&&!bad_write);return 0;}
int test_readback_content_mismatch(void){scenario=10;requested=3;payload[0]=1;payload[1]=9;payload[2]=3;uint8_t expected[3]={1,2,3};CHECK(!diag_compare_bytes(expected,3));CHECK(wait_count==2&&!bad_write);return 0;}
int test_readback_zero_length_blocks(void){scenario=10;requested=0;uint8_t expected[3]={1,2,3};CHECK(!diag_compare_bytes(expected,3));CHECK(wait_count==1&&!bad_write);return 0;}
int test_readback_closes_without_update(void){legacy_setup();regs[0x4c]=1088;regs[0x50]=16448;regs[0x5c]=16448;regs[0x60]=7;regs[0x64]=7;regs[0x58]=16488;bytes8[0x2b]=0x10;regs[0x68]=3;requested=3;payload[0]='a';payload[1]='b';payload[2]='c';CHECK(CH376_DiagVerifyFile("/X.TXT",3,0,"abc",3));CHECK(active_cmd==0x36&&params[0]==0);return 0;}
int test_partial_correction_blocks(void){legacy_setup();bytes8[0x2b]=0x10;regs[0x50]=16448;CHECK(!CH376_DiagEnsureReady());CHECK(!writes32);return 0;}
int test_fat_zero_reserved_entry_blocks(void){legacy_setup();put32(fat_model[0],0,0);CHECK(!CH376_DiagPrepare(false));CHECK(!CH376_DiagEnsureReady());return 0;}
int test_fat_wrong_media_ff_blocks(void){legacy_setup();put32(fat_model[0],0,0x0fffffff);put32(fat_model[1],0,0x0fffffff);CHECK(!CH376_DiagPrepare(false));return 0;}
int test_fat_reserved_one_invalid_blocks(void){legacy_setup();put32(fat_model[0],4,0);put32(fat_model[1],4,0);CHECK(!CH376_DiagPrepare(false));return 0;}
int test_fat_first_sector_mismatch_blocks(void){legacy_setup();put32(fat_model[1],28,0x0fffffff);CHECK(!CH376_DiagPrepare(false));return 0;}
int test_fat_read_error_blocks_and_remounts(void){legacy_setup();unsigned before=mount_count;fat_read_error=true;CHECK(!CH376_DiagPrepare(false));CHECK(mount_count==before+1);CHECK(!CH376_DiagEnsureReady());return 0;}
static void writing_file(void){legacy_setup();regs[0x50]=16448;regs[0x5c]=16448;regs[0x60]=7;regs[0x64]=7;regs[0x68]=56;regs[0x54]=16488;regs[0x58]=16488;}
int test_nonempty_zero_start_cluster_blocks(void){writing_file();regs[0x60]=0;CHECK(!CH376_DiagCanWrite("test"));return 0;}
int test_nonempty_target_in_fat_blocks(void){writing_file();regs[0x58]=16432;CHECK(!CH376_DiagCanWrite("test"));return 0;}
int test_nonempty_cluster_out_of_volume_blocks(void){writing_file();regs[0x64]=0x0ffffff0;CHECK(!CH376_DiagCanWrite("test"));return 0;}
int test_empty_new_file_before_allocation_is_allowed(void){writing_file();regs[0x60]=0;regs[0x64]=0;regs[0x68]=0;regs[0x54]=16448;regs[0x58]=16448;CHECK(CH376_DiagCanWrite("before allocation"));return 0;}
/* 完成后缓冲可能已切回FAT；不能再按正在接收数据的页来判错。 */
int test_finish_allows_nondata_buffer_after_cluster_eof(void){writing_file();regs[0x68]=4096;regs[0x6c]=4096;regs[0x64]=0;regs[0x54]=1120;regs[0x58]=UINT32_MAX;CHECK(CH376_DiagCanFinish());return 0;}
int test_finish_zero_cluster_rejects_directory_update(void){writing_file();regs[0x60]=0;regs[0x6c]=56;CHECK(!CH376_DiagCanFinish());CHECK(CH376_DiagWriteFault());return 0;}
int test_append_allows_aligned_eof_before_allocation(void){writing_file();regs[0x68]=4096;regs[0x6c]=4096;regs[0x64]=0;regs[0x58]=UINT32_MAX;CHECK(CH376_DiagCanWrite("before next allocation"));return 0;}

uint8_t CH376T_BYTE_WRITE_DATA(const uint8_t *data, uint16_t len)
{
    uint8_t status;
    uint16_t remaining = len;
    uint16_t idx = 0;

    if (data == NULL || len == 0) return 0;
    if (!CH376_DiagCanWrite("字节写请求前")) return 0;
    ESP_LOGI(TAG, "[写块排查] 总请求=%u字节", len);

    CH376_Cmd(CMD_BYTE_WRITE); /* 0x3c */
    CH376_Write(len & 0xFF);   /* 长度低字节 */
    CH376_Write((len >> 8) & 0xFF);
    CH376_Cmd_End();

    status = wait();
    if (status != USB_INT_DISK_WRITE) { /* 0x1E */
        ESP_LOGE(TAG, "请求写入失败: 0x%02X", status);
        return 0;
    }

    do {
        /* 0x1E交接阶段只按厂商流程提供数据、继续写入。
         * 此时内部变量窗口可能映射临时数据，禁止插入READ_VAR或其他诊断命令。
         * 请求前检查FAT/文件上下文，最终0x14后再检查持久字段并关闭回读。 */
        CH376_Cmd(CMD_WR_REQ_DATA); /* 0x2d */
        uint8_t n = CH376_Read();
        const uint8_t requested = n;
        if (n > remaining) n = remaining;
        for (uint8_t i = 0; i < n; i++) {
            CH376_Write(data[idx++]);
        }
        remaining -= n;
        CH376_Cmd_End();

        ESP_LOGI(TAG, "[写块排查] 芯片请求=%u 实际提供=%u 已提供=%u 剩余=%u 总长=%u",
                 requested, n, idx, remaining, len);
        if (requested == 0 || requested != n) {
            ESP_LOGW(TAG, "[写块排查] 异常请求长度；保留原算法，不改返回判定，注意重复零进度或超量请求");
        }

        CH376_Cmd(CMD_BYTE_WR_GO); /* 0x3d */
        status = wait();
        if (status == USB_INT_SUCCESS && remaining == 0) { /* 0x14 完成 */
            if (!CH376_DiagCanFinish()) return 0;
            ESP_LOGI(TAG, "写入成功(%u字节)", len);
            CH376_DiagFile("字节写完成、关闭前",NULL);
            return 1;
        }
        /* status==0x1E 表示还有缓冲，继续下一批 */
    } while (status == USB_INT_DISK_WRITE && remaining > 0);

    ESP_LOGE(TAG, "写入失败: 0x%02X 已提供=%u 剩余=%u 总长=%u", status, idx, remaining, len);
    return 0;
}


/* 真实diag和真实字节写函数共同运行；不将诊断保护简化成布尔桩。 */
#include "service/storage/storage.h"
void *memcpy(void*d,const void*s,size_t n){uint8_t*a=d;const uint8_t*b=s;while(n--)*a++=*b++;return d;}
static void prepare_protocol(unsigned length){writing_file();regs[0x6c]=56;scenario=30;protocol_goal=length;}
int test_handshake_single_block_without_interleaved_probe(void){
    prepare_protocol(3);const uint8_t data[3]={'a','b','c'};
    CHECK(CH376T_BYTE_WRITE_DATA(data,3)==1);CHECK(protocol_bad_commands==0);
    CHECK(protocol_written==3&&!protocol_pending&&regs[0x68]==59);
    for(unsigned i=0;i<3;i++)CHECK(protocol_payload[i]==data[i]);return 0;
}
int test_handshake_multiple_blocks_without_interleaved_probe(void){
    prepare_protocol(7);const uint8_t data[7]={1,2,3,4,5,6,7};
    CHECK(CH376T_BYTE_WRITE_DATA(data,7)==1);CHECK(protocol_bad_commands==0);
    CHECK(protocol_written==7&&!protocol_pending&&regs[0x68]==63);
    for(unsigned i=0;i<7;i++)CHECK(protocol_payload[i]==data[i]);return 0;
}
int test_handshake_damaged_fat_stops_before_request(void){
    prepare_protocol(3);s_fat_healthy=false;const uint8_t data[3]={1,2,3};
    CHECK(CH376T_BYTE_WRITE_DATA(data,3)==0);CHECK(!protocol_pending&&protocol_written==0);
    for(unsigned i=0;i<command_count;i++)CHECK(commands[i]!=0x3c);return 0;
}
int test_handshake_new_file_receives_complete_header(void){
    prepare_protocol(strlen(STORAGE_HEADER));protocol_chunk=64;
    regs[0x60]=6;regs[0x64]=6;regs[0x68]=0;regs[0x6c]=0;regs[0x54]=16448;regs[0x58]=16448;
    CHECK(CH376T_BYTE_WRITE_DATA((const uint8_t*)STORAGE_HEADER,strlen(STORAGE_HEADER))==1);
    CHECK(protocol_bad_commands==0&&protocol_written==strlen(STORAGE_HEADER));
    CHECK(regs[0x68]==strlen(STORAGE_HEADER)&&regs[0x6c]==strlen(STORAGE_HEADER));
    for(unsigned i=0;i<strlen(STORAGE_HEADER);i++)CHECK(protocol_payload[i]==(uint8_t)STORAGE_HEADER[i]);return 0;
}
