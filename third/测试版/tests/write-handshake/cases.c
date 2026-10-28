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
