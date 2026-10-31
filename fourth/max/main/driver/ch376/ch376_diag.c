#include "ch376_diag.h"
#include "ch376_spi.h"
#include <stddef.h>
#include <string.h>

/* 地址来自沁恒CH376INC.H；命令定义见CH376手册（一）、（二）。
 * 特别注意0x4C是分区起点，0x50才是数据区起点，不根据名称猜地址。 */
typedef struct {
    bool valid;
    uint32_t partition, total, fat_size, fat2_lba, data_lba, root;
    uint16_t reserved, backup;
    uint8_t spc, fats, media;
} diag_geometry_t;
static diag_geometry_t s_geometry;
static bool s_fat_healthy;
static bool s_write_fault;

static uint16_t diag_u16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t diag_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t diag_read32(uint8_t address)
{
    uint8_t b[4];
    CH376_Cmd(0x0c);
    CH376_Write(address);
    for (unsigned i=0;i<4;i++) b[i]=CH376_Read();
    CH376_Cmd_End();
    uint32_t value=diag_u32(b);
    return value;
}

static uint8_t diag_read8(uint8_t address)
{
    CH376_Cmd(0x0a); CH376_Write(address);
    uint8_t value=CH376_Read(); CH376_Cmd_End();
    return value;
}

/* 读取长度声明的全部字节，即使超过输出容量也排空，避免剩余字节污染下一命令。 */
static uint8_t diag_read_block(uint8_t *out, uint16_t capacity)
{
    CH376_Cmd(CMD_RD_USB_DATA0);
    uint8_t n=CH376_Read();
    for (unsigned i=0;i<n;i++) {
        uint8_t value=CH376_Read();
        if (i<capacity) out[i]=value;
    }
    CH376_Cmd_End();
    return n;
}

static bool diag_read_sector(uint32_t lba, uint8_t *out)
{
    CH376_Cmd(CMD_DISK_READ);
    for (unsigned i=0;i<4;i++) CH376_Write((uint8_t)(lba>>(i*8)));
    CH376_Write(1); CH376_Cmd_End();
    unsigned total=0;
    /* 512字节通常8个64字节块+完成中断；限制轮数，异常探针不会无限循环。 */
    for (unsigned round=0;round<32;round++) {
        uint8_t status=wait();
        if (status==USB_INT_SUCCESS) {
            return total==512;
        }
        if (status!=USB_INT_DISK_READ) {
            return false;
        }
        uint8_t block[64];
        uint8_t n=diag_read_block(block,sizeof(block));
        if (!n || n>sizeof(block) || total+n>512) {
            return false;
        }
        for (unsigned i=0;i<n;i++) out[total++]=block[i];
        CH376_Cmd(CMD_DISK_RD_GO); CH376_Cmd_End();
    }
    return false;
}

static bool diag_parse_bpb(const uint8_t *b, uint32_t partition)
{
    uint16_t reserved=diag_u16(b+14);
    uint32_t total=diag_u32(b+32), fat_size=diag_u32(b+36), root=diag_u32(b+44);
    uint8_t spc=b[13], fats=b[16];
    uint64_t relative=(uint64_t)reserved+(uint64_t)fats*fat_size;
    if (b[510]!=0x55 || b[511]!=0xaa || diag_u16(b+11)!=512 || !reserved ||
        !spc || spc>128 || (spc&(spc-1)) || !fats || fats>2 || !fat_size ||
        diag_u16(b+17)!=0 || diag_u16(b+22)!=0 || root<2 || relative>=total ||
        (uint64_t)partition+total>UINT32_MAX || relative+(uint64_t)(root-2)*spc>=total) {
        return false;
    }
    s_geometry=(diag_geometry_t){.valid=true,.partition=partition,.total=total,.fat_size=fat_size,
        .fat2_lba=partition+reserved+fat_size,.data_lba=partition+(uint32_t)relative,.root=root,
        .reserved=reserved,.backup=diag_u16(b+50),.spc=spc,.fats=fats,.media=b[21]};
    s_fat_healthy=false;
    return true;
}

void CH376_DiagReset(void) {
    memset(&s_geometry,0,sizeof(s_geometry));
    s_fat_healthy=false;
    s_write_fault=false;
}

/* 保留项0/1不是可分配的数据簇。旧损坏盘把FAT[0]清零时，
 * 芯片曾把0号簇用于表头，最终写到数据区前的FAT末尾。
 * 只读检查各份FAT首扇区；不自动修正介质，也不声称检查了整张FAT。 */
static bool diag_check_fat_headers(void)
{
    if (!s_geometry.valid || s_geometry.media<0xf0) return false;
    uint8_t first[512], other[512];
    uint32_t lba=s_geometry.partition+s_geometry.reserved;
    uint32_t expected=0x0fffff00u|s_geometry.media;
    for (unsigned copy=0;copy<s_geometry.fats;copy++) {
        uint8_t *sector=copy?other:first;
        if (!diag_read_sector(lba+copy*s_geometry.fat_size,sector)) {
            return false;
        }
        uint32_t entry0=diag_u32(sector)&0x0fffffffu;
        uint32_t entry1=diag_u32(sector+4)&0x0fffffffu;
        bool ok=entry0==expected && (entry1&0x03ffffffu)==0x03ffffffu;
        if (!ok) return false;
        if (copy) {
            for (unsigned i=0;i<512;i++) if (first[i]!=other[i]) {
                return false;
            }
        }
    }
    return true;
}

bool CH376_DiagPrepare(bool force_read_boot)
{
    bool cached=!force_read_boot && s_geometry.valid;
    s_fat_healthy=false;
    if (!cached) s_geometry.valid=false;
    uint8_t sector[512];
    uint32_t partition=0;
    bool parsed=cached;
    if (!cached && diag_read_sector(0,sector)) {
        /* 超级软盘布局在LBA0直接是BPB；否则只识别主分区FAT32，不猜扩展分区。 */
        if (diag_u16(sector+11)==512 && sector[13] && diag_u16(sector+14)) {
            parsed=diag_parse_bpb(sector,0);
        } else if (sector[510]==0x55 && sector[511]==0xaa) {
            for (unsigned i=0;i<4;i++) {
                const uint8_t *entry=sector+446+i*16;
                if (entry[4]==0x0b || entry[4]==0x0c) {
                    partition=diag_u32(entry+8);
                    break;
                }
            }
            if (partition && diag_read_sector(partition,sector)) parsed=diag_parse_bpb(sector,partition);
        }
    }
    bool fat_ok=parsed && diag_check_fat_headers();
    /* 物理读会改变缓冲区；没有文件打开时完成读并重新挂载，避免遗留读请求进入写盘。 */
    uint8_t restored=CH376T_DISK_MOUNT();
    s_fat_healthy=fat_ok && restored!=0;
    return restored!=0 && (!parsed || s_fat_healthy);
}

uint32_t CH376_DiagLocateResult(uint32_t offset)
{
    uint8_t b[4];
    uint8_t n=diag_read_block(b,sizeof(b));
    if (n!=4) {
        return UINT32_MAX;
    }
    uint32_t lba=diag_u32(b);
    return lba;
}

static uint8_t diag_version(void)
{
    CH376_Cmd(0x01); uint8_t v=CH376_Read(); CH376_Cmd_End(); return v;
}

/* 厂商低版本流程只写芯片内部变量，不向U盘发物理写命令。
 * 不硬编码1024；0xCF必须与读回BPB高字节和实际基址差同时吻合才补偿。 */
static void diag_write32(uint8_t address,uint32_t value)
{
    CH376_Cmd(0x0d); CH376_Write(address);
    for (unsigned i=0;i<4;i++) CH376_Write((uint8_t)(value>>(8*i)));
    CH376_Cmd_End();
}

bool CH376_DiagEnsureReady(void)
{
    if (!s_geometry.valid) { return false; }
    if (!s_fat_healthy || s_write_fault) { return false; }
    uint8_t version=diag_version(), state=diag_read8(0x2b);
    if ((version&0xc0)!=0x40) { return false; }
    if (version<0x43 && state<0x10) {
        CH376_Cmd(CMD_SET_FILE_NAME); CH376_Write(0); CH376_Cmd_End();
        CH376_Cmd(CMD_FILE_OPEN); CH376_Cmd_End();
        uint8_t root_status=wait();
        if (root_status!=USB_INT_SUCCESS) return false;
        uint8_t high=diag_read8(0xcf);
        uint32_t base=diag_read32(0x4c), data=diag_read32(0x50), delta=(uint32_t)high<<8;
        if (data!=s_geometry.data_lba) {
            if (!delta || high!=(s_geometry.reserved>>8) ||
                (uint64_t)data+delta!=s_geometry.data_lba || base!=s_geometry.partition ||
                (uint64_t)base+delta>UINT32_MAX) {
                return false;
            }
            diag_write32(0x4c,base+delta);
            diag_write32(0x50,data+delta);
            diag_write32(0x70,0);
        }
    }
    uint32_t actual=diag_read32(0x50), repeat=diag_read32(0x50), root=diag_read32(0x44);
    uint8_t spc=diag_read8(0x38);
    uint32_t base_after=diag_read32(0x4c);
    uint32_t base_expected=s_geometry.partition+(version<0x43?(s_geometry.reserved&0xff00u):0);
    bool ok=actual==s_geometry.data_lba && repeat==actual && root==s_geometry.root && spc==s_geometry.spc && base_after==base_expected;
    return ok;
}

void CH376_DiagRootPath(const char *name)
{
    /* 厂商FileOpen对旧芯片根路径额外清当前簇；不修改目录或介质。 */
    if (name && (name[0]=='/' || name[0]=='\\') && diag_version()<0x43) diag_write32(0x64,0);
}

bool CH376_DiagCanWrite(const char *stage)
{
    uint32_t data=diag_read32(0x50), dir=diag_read32(0x5c);
    bool ok=s_geometry.valid && s_fat_healthy && !s_write_fault && data==s_geometry.data_lba && dir>=s_geometry.data_lba &&
            (uint64_t)dir<(uint64_t)s_geometry.partition+s_geometry.total;
    uint32_t size=diag_read32(0x68);
    if (ok && size) {
        uint32_t start=diag_read32(0x60),current=diag_read32(0x64),target=diag_read32(0x58);
        uint32_t offset=diag_read32(0x6c);
        uint64_t end=(uint64_t)s_geometry.partition+s_geometry.total;
        uint64_t count=(end-s_geometry.data_lba)/s_geometry.spc;
        /* 整簇EOF尚未分配下一簇时，当前簇/地址可能是结束标记。
         * 允许发送下一次请求；数据交接期间不插入诊断，完成后再检查持久字段。 */
        bool eof=offset==size && offset%(s_geometry.spc*512u)==0;
        bool page=current>=2 && (uint64_t)current<count+2 && target>=s_geometry.data_lba && (uint64_t)target<end;
        ok=size!=UINT32_MAX && start>=2 && (uint64_t)start<count+2 && (page || eof);
    }
    return ok;
}

bool CH376_DiagWriteFault(void) { return s_write_fault; }

/* 完成中断后内部页可能已转到FAT或EOF，不能继续强制它留在数据簇。
 * 请求前已经检查FAT和文件上下文，此处核对将要持久化的首簇、长度和指针；
 * 不能把这项事后校验说成可以回滚之前的介质写入。 */
bool CH376_DiagCanFinish(void)
{
    uint32_t start=diag_read32(0x60),size=diag_read32(0x68),offset=diag_read32(0x6c);
    uint64_t data_sectors=s_geometry.valid?(uint64_t)s_geometry.partition+s_geometry.total-s_geometry.data_lba:0;
    uint64_t count=s_geometry.valid?data_sectors/s_geometry.spc:0;
    bool ok=s_geometry.valid && s_fat_healthy && !s_write_fault && start>=2 &&
            (uint64_t)start<count+2 && size && size!=UINT32_MAX && offset && offset<=size &&
            (uint64_t)size<=data_sectors*512u;
    if (!ok) s_write_fault=true;
    return ok;
}

static bool diag_compare_bytes(const uint8_t *expected,uint16_t length)
{
    if (!expected || !length) return false;
    CH376_Cmd(CMD_BYTE_READ); CH376_Write(length&0xff); CH376_Write(length>>8); CH376_Cmd_End();
    unsigned total=0; bool same=true;
    for (unsigned round=0;round<16;round++) {
        uint8_t status=wait();
        if (status==USB_INT_SUCCESS) {
            return same && total==length;
        }
        if (status!=USB_INT_DISK_READ) { return false; }
        uint8_t block[64]; uint8_t n=diag_read_block(block,sizeof(block));
        if (!n || n>sizeof(block) || total+n>length) { return false; }
        for (unsigned i=0;i<n;i++) {
            if (block[i]!=expected[total+i]) {
                same=false;
            }
        }
        total+=n;
        CH376_Cmd(CMD_BYTE_RD_GO); CH376_Cmd_End();
    }
    return false;
}

bool CH376_DiagVerifyFile(const char *name,uint32_t size,uint32_t offset,const char *text,uint16_t length)
{
    if (!name || !text || !length || (uint64_t)offset+length>size) return false;
    if (!CH376T_DISK_MOUNT() || !CH376_DiagEnsureReady()) { return false; }
    if (CH376T_FILE_OPEN_STATUS(name)!=USB_INT_SUCCESS) { return false; }
    uint32_t got=CH376T_GetFileSize();
    bool ok=CH376_DiagCanWrite("回读文件地址验收") && got==size;
    if (ok) ok=CH376T_BYTE_LOCATE(offset)!=0;
    if (ok) ok=diag_compare_bytes((const uint8_t*)text,length);
    /* 校验只读关闭：参数0，不回写目录长度。失败时也尝试结束校验文件上下文。 */
    CH376_Cmd(CMD_FILE_CLOSE); CH376_Write(0); CH376_Cmd_End();
    uint8_t closed=wait();
    ok=ok && closed==USB_INT_SUCCESS;
    return ok;
}
