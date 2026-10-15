#include "ch376_diag.h"
#include "ch376_spi.h"
#include "esp_log.h"
#include <stddef.h>
#include <string.h>

/* 地址来自沁恒CH376INC.H；命令定义见CH376手册（一）、（二）。
 * 特别注意0x4C是分区起点，0x50才是数据区起点，不根据名称猜地址。 */
static const char *TAG = "CH376地址";
typedef struct {
    bool valid;
    uint32_t partition, total, fat_size, fat2_lba, data_lba, root;
    uint16_t reserved, backup;
    uint8_t spc, fats;
} diag_geometry_t;
static diag_geometry_t s_geometry;

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
    ESP_LOGI(TAG, "[内部变量] 地址=0x%02X 原始=%02X %02X %02X %02X 值=%lu(0x%08lX)",
             address,b[0],b[1],b[2],b[3],(unsigned long)value,(unsigned long)value);
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
    ESP_LOGI(TAG,"[物理只读] 请求整盘绝对LBA=%lu 扇区数=1",(unsigned long)lba);
    CH376_Cmd(CMD_DISK_READ);
    for (unsigned i=0;i<4;i++) CH376_Write((uint8_t)(lba>>(i*8)));
    CH376_Write(1); CH376_Cmd_End();
    unsigned total=0;
    /* 512字节通常8个64字节块+完成中断；限制轮数，异常探针不会无限循环。 */
    for (unsigned round=0;round<32;round++) {
        uint8_t status=wait();
        if (status==USB_INT_SUCCESS) {
            ESP_LOGI(TAG,"[物理只读] LBA=%lu 最终返回=0x14 已读=%u/512",(unsigned long)lba,total);
            return total==512;
        }
        if (status!=USB_INT_DISK_READ) {
            ESP_LOGW(TAG,"[物理只读] LBA=%lu 失败原始码=0x%02X 已读=%u",(unsigned long)lba,status,total);
            return false;
        }
        uint8_t block[64];
        uint8_t n=diag_read_block(block,sizeof(block));
        if (!n || n>sizeof(block) || total+n>512) {
            ESP_LOGW(TAG,"[物理只读] 块长异常=%u 已读=%u；停止探针，稍后重新挂载恢复上下文",n,total);
            return false;
        }
        for (unsigned i=0;i<n;i++) out[total++]=block[i];
        CH376_Cmd(CMD_DISK_RD_GO); CH376_Cmd_End();
    }
    ESP_LOGW(TAG,"[物理只读] 轮数上限到达，不继续查询");
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
        ESP_LOGW(TAG,"[引导扇区] 不是可解析的512字节扇区FAT32，禁止输出猜测的几何结论");
        return false;
    }
    s_geometry=(diag_geometry_t){.valid=true,.partition=partition,.total=total,.fat_size=fat_size,
        .fat2_lba=partition+reserved+fat_size,.data_lba=partition+(uint32_t)relative,.root=root,
        .reserved=reserved,.backup=diag_u16(b+50),.spc=spc,.fats=fats};
    ESP_LOGI(TAG,"[引导扇区] 保留区原始双字节=%02X %02X 完整值=%u(0x%04X) 仅低字节=%u 每簇=%u FAT份数=%u 每份FAT=%lu",
             b[14],b[15],reserved,reserved,b[14],spc,fats,(unsigned long)fat_size);
    ESP_LOGI(TAG,"[几何期望] 分区绝对起点=%lu BPB隐藏扇区=%lu FAT1绝对LBA=%lu FAT2绝对LBA=%lu 数据区绝对LBA=%lu 根簇=%lu 根目录绝对LBA=%llu 总扇区=%lu",
             (unsigned long)partition,(unsigned long)diag_u32(b+28),(unsigned long)(partition+reserved),
             (unsigned long)s_geometry.fat2_lba,(unsigned long)s_geometry.data_lba,(unsigned long)root,
             (unsigned long long)(s_geometry.data_lba+(uint64_t)(root-2)*spc),(unsigned long)total);
    return true;
}

static void diag_geometry_snapshot(const char *stage)
{
    uint32_t partition=diag_read32(0x4c), data=diag_read32(0x50), root=diag_read32(0x44);
    uint32_t data_again=diag_read32(0x50);
    ESP_LOGI(TAG,"[芯片几何] 阶段=%s 内部0x4C基准=%lu 数据区绝对起点=%lu 根目录变量=%lu 每簇=%u 磁盘状态=0x%02X 同一基址复读=%lu",
             stage,(unsigned long)partition,(unsigned long)data,(unsigned long)root,
             diag_read8(0x38),diag_read8(0x2b),(unsigned long)data_again);
    if (data!=data_again) ESP_LOGW(TAG,"[地址异常] 无文件操作间基址复读不一致，优先核对SPI/芯片上下文");
    if (s_geometry.valid) {
        ESP_LOGI(TAG,"[地址对照] 内部0x4C-实际MBR起点=%lld(旧芯片兼容后可非零) 数据基址-BPB期望=%lld扇区",
                 (long long)partition-s_geometry.partition,(long long)data-s_geometry.data_lba);
        if (data!=s_geometry.data_lba) ESP_LOGW(TAG,"[地址异常] 数据区基址不一致，兼容校验通过前禁止建文件和写盘");
    }
}

void CH376_DiagReset(void) { memset(&s_geometry,0,sizeof(s_geometry)); }

bool CH376_DiagPrepare(bool force_read_boot)
{
    diag_geometry_snapshot("原挂载完成、物理查询前");
    if (!force_read_boot && s_geometry.valid) return true;
    s_geometry.valid=false;
    uint8_t sector[512];
    uint32_t partition=0;
    bool parsed=false;
    if (diag_read_sector(0,sector)) {
        /* 超级软盘布局在LBA0直接是BPB；否则只识别主分区FAT32，不猜扩展分区。 */
        if (diag_u16(sector+11)==512 && sector[13] && diag_u16(sector+14)) {
            parsed=diag_parse_bpb(sector,0);
        } else if (sector[510]==0x55 && sector[511]==0xaa) {
            for (unsigned i=0;i<4;i++) {
                const uint8_t *entry=sector+446+i*16;
                if (entry[4]==0x0b || entry[4]==0x0c) {
                    partition=diag_u32(entry+8);
                    ESP_LOGI(TAG,"[分区表] 项=%u 类型=0x%02X 起点=%lu 长度=%lu",i,entry[4],(unsigned long)partition,(unsigned long)diag_u32(entry+12));
                    break;
                }
            }
            if (partition && diag_read_sector(partition,sector)) parsed=diag_parse_bpb(sector,partition);
        }
    }
    if (parsed && s_geometry.backup && s_geometry.backup<s_geometry.reserved) {
        uint16_t reserved=s_geometry.reserved;
        if (diag_read_sector(partition+s_geometry.backup,sector)) {
            ESP_LOGI(TAG,"[备份引导] 原始保留区=%02X %02X 值=%u 主引导=%u 签名=%02X %02X",
                     sector[14],sector[15],diag_u16(sector+14),reserved,sector[510],sector[511]);
            if (diag_u16(sector+14)!=reserved) ESP_LOGW(TAG,"[地址异常] 主/备份引导扇区保留区参数不一致");
        }
    }
    if (!parsed) ESP_LOGW(TAG,"[地址排查] 未拿到可靠引导参数，后续只输出内部原始变量，不输出确定偏移");
    /* 物理读会改变缓冲区；没有文件打开时完成读并重新挂载，避免遗留读请求进入写盘。 */
    uint8_t restored=CH376T_DISK_MOUNT();
    ESP_LOGI(TAG,"[诊断恢复] 物理查询后重新挂载返回=%u；失败时上层停止本次文件操作",restored);
    diag_geometry_snapshot("物理查询后重新挂载");
    return restored!=0;
}

void CH376_DiagFile(const char *stage, const char *name)
{
    uint32_t dir=diag_read32(0x5c), start=diag_read32(0x60), current=diag_read32(0x64);
    uint32_t size=diag_read32(0x68), offset=diag_read32(0x6c), buffer=diag_read32(0x54), lba=diag_read32(0x58);
    ESP_LOGI(TAG,"[文件地址] 阶段=%s 文件=%s 目录绝对LBA=%lu 首簇=%lu 当前簇=%lu 文件长度=%lu 指针=%lu 缓冲LBA=%lu 当前读写LBA=%lu",
             stage,name?name:"未提供",(unsigned long)dir,(unsigned long)start,(unsigned long)current,
             (unsigned long)size,(unsigned long)offset,(unsigned long)buffer,(unsigned long)lba);
    if (s_geometry.valid) {
        if (start>=2 && (uint64_t)(start-2)*s_geometry.spc<s_geometry.total) {
            ESP_LOGI(TAG,"[文件期望] 首簇对应正确绝对LBA=%llu",
                     (unsigned long long)(s_geometry.data_lba+(uint64_t)(start-2)*s_geometry.spc));
        }
        if (dir>=s_geometry.partition+s_geometry.reserved && dir<s_geometry.data_lba) {
            ESP_LOGW(TAG,"[地址异常] 当前目录项位于FAT区域！目录LBA=%lu 正常数据区从=%lu开始",
                     (unsigned long)dir,(unsigned long)s_geometry.data_lba);
        }
    }
}

uint32_t CH376_DiagLocateResult(uint32_t offset)
{
    uint8_t b[4];
    uint8_t n=diag_read_block(b,sizeof(b));
    if (n!=4) {
        ESP_LOGW(TAG,"[定位地址] 返回长度=%u，已全部排空；无法解析LBA",n);
        return UINT32_MAX;
    }
    uint32_t lba=diag_u32(b);
    ESP_LOGI(TAG,"[定位地址] 偏移=%lu 原始=%02X %02X %02X %02X 绝对LBA=%lu(0x%08lX)",
             (unsigned long)offset,b[0],b[1],b[2],b[3],(unsigned long)lba,(unsigned long)lba);
    if (lba==UINT32_MAX) ESP_LOGI(TAG,"[定位地址] FFFFFFFF表示文件末尾，合法；用文件首簇与内部读写LBA辅助判断，不判寻址错误");
    else if (s_geometry.valid && lba>=s_geometry.partition+s_geometry.reserved && lba<s_geometry.data_lba) {
        ESP_LOGW(TAG,"[地址异常] 定位返回LBA位于FAT区域，低于正确数据区基址");
    }
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
    ESP_LOGI(TAG,"[兼容变量] 内部地址=0x%02X 写入=%lu(0x%08lX)，不是写U盘扇区",address,(unsigned long)value,(unsigned long)value);
}

bool CH376_DiagEnsureReady(void)
{
    if (!s_geometry.valid) { ESP_LOGE(TAG,"[写盘拦截] 无可靠BPB，禁止进入文件操作"); return false; }
    uint8_t version=diag_version(), state=diag_read8(0x2b);
    uint32_t before_base=diag_read32(0x4c), before_data=diag_read32(0x50);
    ESP_LOGI(TAG,"[兼容验证] 版本=0x%02X 状态=0x%02X 处理前0x4C=%lu 0x50=%lu 期望数据区=%lu",
             version,state,(unsigned long)before_base,(unsigned long)before_data,(unsigned long)s_geometry.data_lba);
    if ((version&0xc0)!=0x40) { ESP_LOGE(TAG,"[写盘拦截] 芯片版本读值无效"); return false; }
    if (version<0x43 && state<0x10) {
        CH376_Cmd(CMD_SET_FILE_NAME); CH376_Write(0); CH376_Cmd_End();
        CH376_Cmd(CMD_FILE_OPEN); CH376_Cmd_End();
        uint8_t root_status=wait();
        ESP_LOGI(TAG,"[兼容验证] 空文件名打开根目录原始返回=0x%02X",root_status);
        if (root_status!=USB_INT_SUCCESS) return false;
        uint8_t high=diag_read8(0xcf);
        uint32_t base=diag_read32(0x4c), data=diag_read32(0x50), delta=(uint32_t)high<<8;
        ESP_LOGI(TAG,"[兼容验证] 0xCF=0x%02X BPB高字节=0x%02X 修正量=%lu 当前0x4C=%lu 0x50=%lu",
                 high,s_geometry.reserved>>8,(unsigned long)delta,(unsigned long)base,(unsigned long)data);
        if (data==s_geometry.data_lba) {
            ESP_LOGI(TAG,"[兼容验证] 数据基址已正确，不重复补偿");
        } else {
            if (!delta || high!=(s_geometry.reserved>>8) ||
                (uint64_t)data+delta!=s_geometry.data_lba || base!=s_geometry.partition ||
                (uint64_t)base+delta>UINT32_MAX) {
                ESP_LOGE(TAG,"[写盘拦截] 旧版本修正条件不吻合，不猜测或强行加偏移"); return false;
            }
            diag_write32(0x4c,base+delta);
            diag_write32(0x50,data+delta);
            diag_write32(0x70,0);
        }
    } else {
        ESP_LOGI(TAG,"[兼容验证] 新版本或文件系统已就绪，跳过补偿，只校验地址");
    }
    uint32_t actual=diag_read32(0x50), repeat=diag_read32(0x50), root=diag_read32(0x44);
    uint8_t spc=diag_read8(0x38);
    uint32_t base_after=diag_read32(0x4c);
    uint32_t base_expected=s_geometry.partition+(version<0x43?(s_geometry.reserved&0xff00u):0);
    bool ok=actual==s_geometry.data_lba && repeat==actual && root==s_geometry.root && spc==s_geometry.spc && base_after==base_expected;
    ESP_LOGI(TAG,"[兼容基准] 内部0x4C实际=%lu 厂商兼容流程期望=%lu 实际MBR起点=%lu",
             (unsigned long)base_after,(unsigned long)base_expected,(unsigned long)s_geometry.partition);
    ESP_LOGI(TAG,"[兼容结论] 处理后0x4C=%lu 数据区=%lu 复读=%lu 差值=%lld 根簇=%lu 每簇=%u 结果=%s",
             (unsigned long)base_after,(unsigned long)actual,(unsigned long)repeat,
             (long long)actual-s_geometry.data_lba,(unsigned long)root,spc,ok?"通过":"失败，禁止写盘");
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
    bool ok=s_geometry.valid && data==s_geometry.data_lba && dir>=s_geometry.data_lba &&
            (uint64_t)dir<(uint64_t)s_geometry.partition+s_geometry.total;
    ESP_LOGI(TAG,"[写盘保护] 阶段=%s 数据基址=%lu 目录LBA=%lu 结果=%s",stage,(unsigned long)data,(unsigned long)dir,
             ok?"允许":"地址不符，拦截写入");
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
            ESP_LOGI(TAG,"[内容回读] 请求=%u 实读=%u 逐字节比较=%s",length,total,same && total==length?"一致":"失败");
            return same && total==length;
        }
        if (status!=USB_INT_DISK_READ) { ESP_LOGE(TAG,"[内容回读] 失败状态=0x%02X 已读=%u",status,total); return false; }
        uint8_t block[64]; uint8_t n=diag_read_block(block,sizeof(block));
        if (!n || n>sizeof(block) || total+n>length) { ESP_LOGE(TAG,"[内容回读] 块长度异常=%u 已读=%u",n,total); return false; }
        for (unsigned i=0;i<n;i++) {
            if (block[i]!=expected[total+i]) {
                if (same) ESP_LOGE(TAG,"[内容回读] 首个差异位置=%u 期望=0x%02X 实际=0x%02X",total+i,expected[total+i],block[i]);
                same=false;
            }
        }
        total+=n;
        CH376_Cmd(CMD_BYTE_RD_GO); CH376_Cmd_End();
    }
    ESP_LOGE(TAG,"[内容回读] 轮数到达上限"); return false;
}

bool CH376_DiagVerifyFile(const char *name,uint32_t size,uint32_t offset,const char *text,uint16_t length)
{
    ESP_LOGI(TAG,"[持久化验证] 开始 文件=%s 期望总长=%lu 比较偏移=%lu 比较字节=%u",
             name,(unsigned long)size,(unsigned long)offset,length);
    if (!name || !text || !length || (uint64_t)offset+length>size) return false;
    if (!CH376T_DISK_MOUNT() || !CH376_DiagEnsureReady()) { ESP_LOGE(TAG,"[持久化验证] 失败阶段=重挂载或兼容校验"); return false; }
    if (CH376T_FILE_OPEN_STATUS(name)!=USB_INT_SUCCESS) { ESP_LOGE(TAG,"[持久化验证] 失败阶段=重新打开"); return false; }
    uint32_t got=CH376T_GetFileSize();
    bool ok=CH376_DiagCanWrite("回读文件地址验收") && got==size;
    ESP_LOGI(TAG,"[持久化验证] 重新打开实际长度=%lu 期望=%lu",(unsigned long)got,(unsigned long)size);
    if (ok) ok=CH376T_BYTE_LOCATE(offset)!=0;
    if (ok) ok=diag_compare_bytes((const uint8_t*)text,length);
    /* 校验只读关闭：参数0，不回写目录长度。失败时也尝试结束校验文件上下文。 */
    CH376_Cmd(CMD_FILE_CLOSE); CH376_Write(0); CH376_Cmd_End();
    uint8_t closed=wait();
    ok=ok && closed==USB_INT_SUCCESS;
    ESP_LOGI(TAG,"[持久化验证] 只读关闭返回=0x%02X 结论=%s；电脑接盘验证仍需单独确认",closed,ok?"地址/长度/内容回读通过":"失败");
    return ok;
}
