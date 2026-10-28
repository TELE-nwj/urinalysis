
/*
 * CH376芯片 软件模拟SPI串行连接的硬件抽象层 - ESP32-S3版
 * 实现文件
 */
#include "ch376_spi.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

//=============================================================================
// 内部宏
//=============================================================================
#define CH376_SPI_SCS_HIGH()  gpio_set_level(CH376_SPI_SCS_GPIO, 1)
#define CH376_SPI_SCS_LOW()   gpio_set_level(CH376_SPI_SCS_GPIO, 0)
#define CH376_SPI_SDI_SET(v)  gpio_set_level(CH376_SPI_SDI_GPIO, v)
#define CH376_SPI_SDO_GET()   gpio_get_level(CH376_SPI_SDO_GPIO)
#define CH376_SPI_SCK_HIGH()  gpio_set_level(CH376_SPI_SCK_GPIO, 1)
#define CH376_SPI_SCK_LOW()   gpio_set_level(CH376_SPI_SCK_GPIO, 0)

//=============================================================================
// 静态变量
//=============================================================================
static const char *TAG = "CH376T";

//=============================================================================
// 延时函数
//=============================================================================
static inline void mDelay0_5uS(void)
{
    esp_rom_delay_us(mDelay_uS);  // 至少0.5us
}

static inline void mDelayuS(uint32_t us)
{
    esp_rom_delay_us(us);
}

//=============================================================================
// 初始化
//=============================================================================
void CH376_PORT_INIT(void)
{
    // 配置输出引脚
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    io_conf.pin_bit_mask = (1ULL << CH376_SPI_SCS_GPIO) |
                           (1ULL << CH376_SPI_SCK_GPIO) |
                           (1ULL << CH376_SPI_SDI_GPIO);
    gpio_config(&io_conf);
    
    // 配置SDO输入引脚
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << CH376_SPI_SDO_GPIO);
    gpio_config(&io_conf);
    
    // 配置中断引脚 (如果使用)
    if (CH376_INT_GPIO != 0) {
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pin_bit_mask = (1ULL << CH376_INT_GPIO);
        gpio_config(&io_conf);
    }
    
    // 默认状态
    CH376_SPI_SCS_HIGH();
    CH376_SPI_SCK_HIGH();
    CH376_SPI_SDI_SET(0);
    
    ESP_LOGI(TAG, "SPI 初始化完成: SCS=%d, SDI=%d, SDO=%d, SCK=%d, INT=%d",
             CH376_SPI_SCS_GPIO, CH376_SPI_SDI_GPIO, 
             CH376_SPI_SDO_GPIO, CH376_SPI_SCK_GPIO,
             CH376_INT_GPIO);
}
// ==================== 底层读写操作 ====================
//SPI输出
void ch376_spi_OutByte(uint8_t d)
{
    for (int i = 0; i < 8; i++) {
        CH376_SPI_SCK_LOW();
        CH376_SPI_SDI_SET((d & 0x80) ? 1 : 0);
        d <<= 1;
        CH376_SPI_SCK_HIGH();
        mDelay0_5uS();
    }
}
//SPI输入
uint8_t ch376_spi_InByte(void)
{
    uint8_t d = 0;
    for (int i = 0; i < 8; i++) {
        CH376_SPI_SCK_LOW();
        mDelay0_5uS();
        d <<= 1;
        if (CH376_SPI_SDO_GET()) {
            d |= 0x01;
        }
        CH376_SPI_SCK_HIGH();
        mDelay0_5uS();
    }
    return d;
}
//结束命令
void CH376_Cmd_End(void)
{
    CH376_SPI_SCS_HIGH();
}
//写命令
void CH376_Cmd(uint8_t mCmd)
{
    CH376_SPI_SCS_HIGH();
    mDelay0_5uS();
    CH376_SPI_SCS_LOW();
    mDelay0_5uS();
    ch376_spi_OutByte(mCmd);
    mDelay0_5uS();
    mDelay0_5uS();
    mDelay0_5uS();  // 确保 >1.5us
}
//写数据
void CH376_Write(uint8_t mData)
{
    ch376_spi_OutByte(mData);
    mDelay0_5uS();
}
//读数据
uint8_t CH376_Read(void)
{
    mDelay0_5uS();
    return ch376_spi_InByte();
}

//=============================================================================
// 中断查询
//=============================================================================
uint8_t wait(void)
{
    uint32_t timeout_counter = 0;
    /* 超时上限：DISK_MOUNT 挂载大容量U盘可能耗时较久，放宽到 ~7s */
    const uint32_t timeout_max = 500000;
// 轮询等待中断（INT引脚拉低）
    ESP_LOGI(TAG, "等待CH376T中断响应...");
    timeout_counter = 0;
    while (gpio_get_level(CH376_INT_GPIO) == 1) {
        // 简单延时
        esp_rom_delay_us(10);  // 10微秒延时
        timeout_counter++;
        // 周期让出 CPU，避免饿死空闲任务/看门狗(仅任务上下文调用)
        if ((timeout_counter & 0xFFu) == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        
        // 超时检测
        if (timeout_counter > timeout_max) {
            ESP_LOGE(TAG, "等待中断超时！");
            CH376_Cmd_End();
            return 0xff;
        }
        
        // 每10000次计数打印一次调试信息
        if (timeout_counter % 50000 == 0) {
            ESP_LOGI(TAG, "仍在等待中断... 计数: %lu", timeout_counter);
        }
    }
    ESP_LOGI(TAG, "检测到中断，INT引脚已拉低");
    return CH376_GetStatus();
}

uint8_t CH376_GetStatus(void)
{
    uint8_t status;
    CH376_Cmd(CMD_GET_STATUS);
    status = CH376_Read();
    CH376_Cmd_End();
    ESP_LOGI(TAG, "中断代码: 0x%02X", status);
    return status;
}
//=============================================================================
// CH376T 命令函数
//=============================================================================
// 获取芯片版本号0x01
uint8_t CH376T_Get_IC_Ver(void)
{
    uint8_t version = 0;
    CH376_Cmd(CMD_GET_IC_VER);
    version = CH376_Read();  // 读取版本号
    CH376_Cmd_End();
    ESP_LOGI(TAG, "版本获取0x01: 0x%02X", version);
    return version;
}

// 设置串口波特率0x02
uint8_t CH376T_SetBaudrate(void)
{
    uint8_t baudrate = 0x02;
    uint8_t baudrate_constant = 0xB2;
    // 分频系数 分频常数 串口通讯波特率（bps） 误差 
    // 02H B2H    9600 0.16％ 
    // 02H D9H   19200 0.16％ 
    // 03H 98H   57600 0.16％ 
    // 参数表：03H CCH  115200 0.16％ 
    // 03H F3H  460800 0.16％ 
    // 07H F3H  921600 0.16％ 
    // 03H C4H  100000 0％ 
    // 参数表：03H FAH 1000000 0％ 
    // 参数表：03H FEH 3000000 0％ 
    // 02H 常数 计算公式： 750000/(256-常数)  
    // 03H 常数 计算公式：6000000/(256-常数)  
    CH376_Cmd(CMD_SET_BAUDRATE);  // 0x02
    CH376_Write(baudrate);          // 分频系数
    CH376_Write(baudrate_constant); // 波特率常数
    CH376_Cmd_End();                                 
    
    ESP_LOGI(TAG, "设置波特率0x02: 分频系数=0x%02X, 分频常数=0x%02X", baudrate, baudrate_constant);
    return 1;
}

// 进入低功耗睡眠挂起状态0x03
void CH376T_EnterSleep(void)
{
    CH376_Cmd(CMD_ENTER_SLEEP);  // 0x03
    CH376_Cmd_End();
    ESP_LOGI(TAG, "进入低功耗睡眠挂起状态0x03");
}

// 执行硬件复位0x05
void CH376T_ResetAll(void)
{
    CH376_Cmd(CMD_RESET_ALL);  // 0x05
    CH376_Cmd_End();   
    // 硬件复位后需要等待芯片重新初始化
    esp_rom_delay_us(pdMS_TO_TICKS(35));  // 至少延时 35ms
    ESP_LOGI(TAG, "执行硬件复位0x05");
}

// 检查芯片是否存在测试通讯接口和工作状态 0x06
uint8_t CH376T_CheckExist(void)
{
    uint8_t test_data = 0x57;
    uint8_t rx_data = 0;
    CH376_Cmd(CMD_CHECK_EXIST);  // 0x06
    CH376_Write(test_data);
    rx_data = CH376_Read();
    CH376_Cmd_End();
    // 正确显示期望值（需要强制转换）
    uint8_t expected = (uint8_t)(~test_data);  // 说明：0xAA
    ESP_LOGI(TAG, "芯片存在测试0x06: 0x%02X",test_data);
    ESP_LOGI(TAG, "期望值: 0x%02X", expected);
    ESP_LOGI(TAG, "实际值: 0x%02X", rx_data);
    if (expected == rx_data)
    {
        ESP_LOGI(TAG, "CH376T在线");
        return 1;
    }
    else 
    {
        return 0;
    }
}

// 设置SPI和SDO的中断方式 0x0b
void CH376T_SetSdoInt(void)
{
    uint8_t mode1 = 0x10;
    // 10H禁止SDO引脚用于中断输出，其在SCS片选无效时三态输出禁止，
    // 便于与其它设备共享单片机的SPI总线；
    // 保留的未启用代码：uint8_t mode2 = 0x90;
    // 90H设置SDO引脚一直处于输出状态，在SCS片选无效时兼做中断请求输出，
    // 等效于INT#引脚，供单片机查询中断请求状态。
    CH376_Cmd(CMD_SET_SDO_INT);  // 0x0b
    CH376_Write(0x16);             // 必须发送参数 0x16
    CH376_Write(mode1);
    CH376_Cmd_End();
    ESP_LOGI(TAG, "设置 SPI 和 SDO 的中断方式: 0x%02X", mode1);
}

// 获取当前文件长度（返回 4 字节）0x0c
uint32_t CH376T_GetFileSize(void)
{
    uint32_t file_size = 0;
    uint8_t byte0, byte1, byte2, byte3;
    
    CH376_Cmd(CMD_GET_FILE_SIZE);  // 0x0C，不需要参数
    CH376_Write(0x68);               // 必须发送参数 0x68
    byte0 = CH376_Read();       // 读取第 1 字节（最低位）
    byte1 = CH376_Read();       // 读取第 2 字节
    byte2 = CH376_Read();       // 读取第 3 字节
    byte3 = CH376_Read();       // 读取第 4 字节（最高位）
    CH376_Cmd_End();
    
    // 组合成 32 位长度（小端模式：低字节在前）
    file_size = byte0 | (byte1 << 8) | (byte2 << 16) | ((uint32_t)byte3 << 24);
    ESP_LOGI(TAG,"获取当前文件长度");
    ESP_LOGI(TAG, "文件长度: %lu 字节 (0x%08lX)", file_size, file_size);
    ESP_LOGI(TAG, "原始数据: %02X %02X %02X %02X", byte0, byte1, byte2, byte3);
    
    return file_size;
}

// 设置 USB 模式
uint8_t CH376T_SetUsbMode(void)
{
    uint8_t status;
    uint8_t mode= 0x06;
    // 模式代码为00H时切换到未启用的USB设备方式（上电或复位后的默认方式）； 
    // 模式代码为01H时切换到已启用的USB设备方式，外部固件模式（串口连接方式不支持）； 
    // 模式代码为02H时切换到已启用的USB设备方式，内置固件模式； 
    // 模式代码为03H时切换到SD卡主机模式，用于管理和存取SD卡中的文件； 
    // 模式代码为04H时切换到未启用的USB主机方式； 
    // 模式代码为05H时切换到已启用的USB主机方式，不产生SOF包； 
    // 模式代码为06H时切换到已启用的USB主机方式，自动产生SOF包； 
    // 模式代码为07H时切换到已启用的USB主机方式，复位USB总线；
    CH376_Cmd(CMD_SET_USB_MODE);  // 0x15
    CH376_Write(mode);              // 模式参数
    /* 手册要求：切到 USB 主机模式(0x06)后，芯片内部要初始化 USB 主机，
       必须延时(约 50ms)再读"操作状态"，否则读到的是无效值 */
    vTaskDelay(pdMS_TO_TICKS(50));
    status = CH376_Read();     // 读取操作状态(注意: SET_USB_MODE 属"操作状态"命令, 不产生中断, 直接读)
    CH376_Cmd_End();

    /* 模式 0x06 为已启用的 USB 主机方式(自动产生 SOF 包)。
     * 成功码说明: 按 CH376 手册, SET_USB_MODE 完成后输出的是"操作状态",
     * 其成功码为 0x51 CMD_RET_SUCCESS(不是中断类命令的 0x14)。
     * 这里同时认 0x51 与 0x14 以兼容时序/不同固件。 */
    if (status == CMD_RET_SUCCESS || status == USB_INT_SUCCESS)
    {
        ESP_LOGI(TAG, "设置 USB 模式成功 (0x%02X)", status);
        return 1;
    }
    else
    {
        ESP_LOGI(TAG, "设置 USB 模式失败, 状态: 0x%02X", status);
        return 0;
    }
}

// 检测USB连接状态 0x30
/* 注意：写法与参考工程(已验证可跑)完全一致 —— 发完命令后写一个 0x00，
   且【不调用】CH376_Cmd_End()，直接进 wait()。不要"顺手改规范"。 */
uint8_t CH376T_CMD_DISK_CONNECT(void)
{
    uint8_t status;
    CH376_Cmd(CMD_DISK_CONNECT);
    CH376_Write(0x00);
    // 保留的未启用代码：CH376_Cmd_End();

    status = wait();
    if (status == USB_INT_SUCCESS)
    {
        ESP_LOGI(TAG, "USB连接成功");
        return 1;
    }
    else
    {
        ESP_LOGI(TAG, "USB连接状态: 0x%02X", status);
        return 0;
    }
}
    
/* 检测磁盘在线状态，返回原始中断状态码(不做 0/1 转换)。
   写法与 CH376T_CMD_DISK_CONNECT 完全一致，只是把状态码原样给上层。 */
uint8_t CH376T_DiskCheckRaw(void)
{
    CH376_Cmd(CMD_DISK_CONNECT);
    CH376_Write(0x00);
    return wait();
}

// 初始化磁盘并测试磁盘是否就绪 0x31
uint8_t CH376T_DISK_MOUNT(void)
{
    uint8_t status;
    CH376_Cmd(CMD_DISK_MOUNT);     /* 0x31: 无输入数据 */
    CH376_Cmd_End();
    status = wait();
    if (status == USB_INT_SUCCESS)
    {
        ESP_LOGI(TAG, "磁盘挂载成功");
        return 1;
    }
    ESP_LOGE(TAG, "磁盘挂载失败, 状态: 0x%02X", status);
    return 0;
}

//打开文件0x2f  0x32
uint8_t CH376T_SET_FILE_NAME(void)
{
    uint8_t status;
    char *file_name = "ASD.TXT";  // 或者 char file_name[] = "HAOJIAHUO"
    uint8_t len = strlen(file_name);
    
    // 发送设置文件名命令
    CH376_Cmd(CMD_SET_FILE_NAME);//0x2f
    for (uint8_t i = 0; i < len; i++) 
    {
        //ESP_LOGI(TAG, "文件名字符: 0x%02X", file_name[i]);
        CH376_Write(file_name[i]);
    }
    // 写入字符串结束符
    CH376_Write(0x00);
    CH376_Cmd_End();
    CH376_Cmd(CMD_FILE_OPEN);//0x32
    // 写入文件名
    status = wait();
    if (status == USB_INT_SUCCESS)
    {
        ESP_LOGI(TAG, "文件打开成功"); 
        return 1;
    }
    else
    {
        ESP_LOGI(TAG, "文件打开失败"); 
        return 0;
    }
}

//创建文件0x2f  0x34
uint8_t CH376T_FILE_CREATE(void)
{
    uint8_t status;
    char *file_name = "/ASD.TXT";  // 或者 char file_name[] = "HAOJIAHUO"
    uint8_t len = strlen(file_name);
    
    // 发送设置文件名命令
    CH376_Cmd(CMD_SET_FILE_NAME);    
    // 写入文件名
    for (uint8_t i = 0; i < len; i++) {
        CH376_Write(file_name[i]);
    }
    // 写入字符串结束符
    CH376_Write(0x00);
    CH376_Cmd_End();

    CH376_Cmd(CMD_FILE_CREATE);

    status = wait();
    if(status == USB_INT_SUCCESS)
    {
        ESP_LOGI(TAG, "创建文件成功");
        return 1;
    }
    else 
    {
        ESP_LOGI(TAG, "创建文件不成功");
        return 0;
    }
}

/* 发送"设置文件名"并结束命令相位: name 可为 "XXX.TXT" 或 "/XXX.TXT" */
static void ch376_send_filename(const char *name)
{
    CH376_Cmd(CMD_SET_FILE_NAME);
    while (name && *name) {
        CH376_Write((uint8_t)*name);
        name++;
    }
    CH376_Write(0x00);       /* 结束符 */
    CH376_Cmd_End();
}

/* 按名字打开文件(不存在则失败) */
uint8_t CH376T_FILE_OPEN_STATUS(const char *name)
{
    if (!name) return 0xff;
    ch376_send_filename(name);
    CH376_Cmd(CMD_FILE_OPEN);
    return wait();
}

uint8_t CH376T_FILE_OPEN_NAME(const char *name)
{
    uint8_t status = CH376T_FILE_OPEN_STATUS(name);
    if (status == USB_INT_SUCCESS) {
        ESP_LOGI(TAG, "打开文件成功: %s", name);
        return 1;
    }
    ESP_LOGE(TAG, "打开文件失败: %s (0x%02X)", name, status);
    return 0;
}

/* 按名字创建(覆盖)文件 */
uint8_t CH376T_FILE_CREATE_NAME(const char *name)
{
    if (!name) return 0;
    ch376_send_filename(name);
    CH376_Cmd(CMD_FILE_CREATE);
    uint8_t status = wait();
    if (status == USB_INT_SUCCESS) {
        ESP_LOGI(TAG, "创建文件成功: %s", name);
        return 1;
    }
    ESP_LOGE(TAG, "创建文件失败: %s (0x%02X)", name, status);
    return 0;
}

uint8_t CH376T_BYTE_WRITE(void)
{
    uint8_t status;
    uint8_t len_char;
    char *file = "hello world\n";
    uint8_t len = strlen(file);
    
    ESP_LOGI(TAG, "写入数据: %s (%d字节)", file, len);
    
    // 1. 请求写入
    CH376_Cmd(CMD_BYTE_WRITE);//0x3c
    CH376_Write(len);    // 长度低字节
    CH376_Write(0);      // 长度高字节
    CH376_Cmd_End();
    
    // 2. 等待准备好
    status = wait();
    if (status != 0x1E) {
        ESP_LOGE(TAG, "请求写入失败: 0x%02X", status);
        return 0;
    }
    
    // 3. 写入数据
    CH376_Cmd(CMD_WR_REQ_DATA);//0x2d
    len_char = CH376_Read();
    ESP_LOGE(TAG, "剩余写入字节: 0x%02X", len_char);
    for (uint8_t i = 0; i < len_char; i++) {
        CH376_Write(file[i]);
    }
    CH376_Cmd_End();
    
    // 4. 继续写入
    CH376_Cmd(CMD_BYTE_WR_GO);//0x3d
    CH376_Cmd_End();
    
    // 5. 等待完成
    status = wait();
    
    if (status == USB_INT_SUCCESS) 
    {
        ESP_LOGI(TAG, "✓ 写入成功");
        return 1;
    } 
    else 
    {
        ESP_LOGE(TAG, "✗ 写入失败: 0x%02X", status);
        return 0;
    }
}
/* 可写任意数据(字节写, 支持分批到完成)。data 指向内容，len 为字节数。 */
uint8_t CH376T_BYTE_WRITE_DATA(const uint8_t *data, uint16_t len)
{
    uint8_t status;
    uint16_t remaining = len;
    uint16_t idx = 0;

    if (data == NULL || len == 0) return 0;

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
        CH376_Cmd(CMD_WR_REQ_DATA); /* 0x2d */
        uint8_t n = CH376_Read();
        if (n > remaining) n = remaining;
        for (uint8_t i = 0; i < n; i++) {
            CH376_Write(data[idx++]);
        }
        remaining -= n;
        CH376_Cmd_End();

        CH376_Cmd(CMD_BYTE_WR_GO); /* 0x3d */
        status = wait();
        if (status == USB_INT_SUCCESS && remaining == 0) { /* 0x14 完成 */
            ESP_LOGI(TAG, "写入成功(%u字节)", len);
            return 1;
        }
        /* status==0x1E 表示还有缓冲，继续下一批 */
    } while (status == USB_INT_DISK_WRITE && remaining > 0);

    ESP_LOGE(TAG, "写入失败: 0x%02X", status);
    return 0;
}

//关闭文件 0x36 
uint8_t CH376T_FILE_CLOSE(void)
{
    uint8_t status;
    CH376_Cmd(CMD_FILE_CLOSE);//0x36
    CH376_Write(0x01);   /* 1 = 允许自动更新文件长度(否则写盘长度不更新) */
    CH376_Cmd_End();
    // 等待中断
    status = wait();
    // 检查状态
    if (status == 0x14)
    {  // 说明：USB_INT_SUCCESS
        ESP_LOGI(TAG, "关闭文件成功");
        return 1;
    }
    else 
    {
        ESP_LOGE(TAG, "关闭文件失败，状态码: 0x%02X", status);
        return 0;
    }
}



//以字节为单位移动当前文件指针 0x39
uint8_t CH376T_BYTE_LOCATE(uint32_t offset)
{
    CH376_Cmd(CMD_BYTE_LOCATE);            /* 0x39 */
    CH376_Write((uint8_t)(offset & 0xFF));
    CH376_Write((uint8_t)((offset >> 8) & 0xFF));
    CH376_Write((uint8_t)((offset >> 16) & 0xFF));
    CH376_Write((uint8_t)((offset >> 24) & 0xFF));
    CH376_Cmd_End();

    uint8_t status = wait();
    if (status == USB_INT_SUCCESS) {       /* 0x14 */
        ESP_LOGI(TAG, "文件指针定位到: %lu", (unsigned long)offset);
        return 1;
    }
    ESP_LOGE(TAG, "文件指针定位失败: 0x%02X", status);
    return 0;
}


// 读文件 0x3a 0x27 0x3b
uint8_t CH376T_BYTE_READ(void)
{
    uint8_t status;
    uint8_t data[64];
    uint8_t len = 0;
    
    ESP_LOGI(TAG, "开始读取文件...");
    
    // 1. 请求读取64字节
    CH376_Cmd(CMD_BYTE_READ); // 0x3a
    CH376_Write(64);          // 请求字节数低字节
    CH376_Write(0);           // 请求字节数高字节
    CH376_Cmd_End();
    
    // 2. 等待数据就绪（芯片会通过 INT 中断请求）
    status = wait();
    if (status != USB_INT_DISK_READ) {   // 0x1D 表示数据可读
        ESP_LOGE(TAG, "读数据请求未就绪: 0x%02X", status);
        return 0;
    }
    
    // 3. 从缓冲区读取本次可用的数据
    CH376_Cmd(CMD_RD_USB_DATA0); // 0x27
    len = CH376_Read();          // 第一个字节是本次可读长度
    if (len > sizeof(data) - 1) {
        len = sizeof(data) - 1;  // 防止越界，预留结束符位置
    }
    for (uint8_t i = 0; i < len; i++) {
        data[i] = CH376_Read();  // 读取数据
    }
    CH376_Cmd_End();
    
    // 4. 打印读取到的数据
    data[len] = '\0';
    ESP_LOGI(TAG, "读取到 %d 字节: %s", len, data);
    
    // 5. 请求继续读取（若文件还有剩余内容）
    CH376_Cmd(CMD_BYTE_RD_GO); // 0x3b
    CH376_Cmd_End();
    
    // 6. 等待本次读出的确认中断
    status = wait();
    if (status == USB_INT_DISK_READ) {
        ESP_LOGI(TAG, "读取进行中，可继续读取...");
    } else if (status == USB_INT_SUCCESS) {
        ESP_LOGI(TAG, "文件读取完成");
    }
    
    return (len > 0) ? 1 : 0;  // 成功返回1，失败返回0
}


