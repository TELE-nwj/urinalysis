#ifndef CH376_SPI_H
#define CH376_SPI_H
#include <stdint.h>
#include <stdbool.h>
#include "bsp/pin_map.h"

/* 引脚统一收敛到 bsp 层 */
#define CH376_SPI_SCS_GPIO    BSP_PIN_CH376_SCS
#define CH376_SPI_SDI_GPIO    BSP_PIN_CH376_SDI
#define CH376_SPI_SDO_GPIO    BSP_PIN_CH376_SDO
#define CH376_SPI_SCK_GPIO    BSP_PIN_CH376_SCK
#define CH376_INT_GPIO        BSP_PIN_CH376_INT

//此处为兼顾CH376S
// 保留的未启用代码：#define CH376_WR#  GPIO_NUM_18 
// 保留的未启用代码：#define CH376_RD#  GPIO_NUM_18
//=============================================================================
// 时序配置
//=============================================================================
#define mDelay_uS 2



//=============================================================================
// 命令配置
//=============================================================================
//======命令名称=============代码======输入数据=======输出数据======命令用途
#define CMD_GET_IC_VER      0x01  //                 版本号        获取芯片及固件版本
#define CMD_SET_BAUDRATE    0x02  //   分频系数常数   操作状态      设置串口波特率
#define CMD_ENTER_SLEEP     0x03  //                              进入低功耗睡眠挂起状态
#define CMD_RESET_ALL       0x05  //                 35ms         执行硬件复位
#define CMD_CHECK_EXIST     0x06  //   任意数据       按位取反      测试通讯接口和工作状态
#define CMD_SET_SDO_INT     0x0b  //   数据16H中断方式              设置SPI和SDO的中断方式
#define CMD_GET_FILE_SIZE   0x0c  //   数据68H        文件长度      获取当前文件长度
#define CMD_SET_USB_MODE    0x15  //   模式代码       操作状态      设置USB工作模式
#define CMD_GET_STATUS      0x22  //   空空空空       中断状态      获取中断状态并取消中断请求 
#define CMD_RD_USB_DATA0    0x27  //   空空空空       数据长度      从当前USB中断的端点缓冲区或者主机端点的接收缓冲区读取数据块
#define CMD_WR_HOST_DATA    0x2c  //   数据长度       空空空        向USB主机端点的发送缓冲区写入数据块
#define CMD_WR_REQ_DATA     0x2d  //   数据流         数据长度       向内部指定缓冲区 写入请求的数据块
#define CMD_WR_OFS_DATA     0x2e  //   地址 长度 数据  空空空        向内部缓冲区指定偏移地址 写入数据块
#define CMD_SET_FILE_NAME   0x2f  //   字符串         空空空        设置将要操作的文件的文件名
#define CMD_DISK_CONNECT    0x30  //   空空空空       空空空        检查磁盘是否连接 
#define CMD_DISK_MOUNT      0x31  //   空空空空       空空空        初始化磁盘并测试磁盘是否就绪 
#define CMD_FILE_OPEN       0x32  //   空空空空       空空空        打开文件或目录，枚举文件和目录
#define CMD_FILE_ENUM_GO    0x33  //   空空空空       空空空        继续枚举文件和目录
#define CMD_FILE_CREATE     0x34  //   空空空空       空空空        新建文件 
#define CMD_FILE_ERASE      0x35  //   空空空空       空空空        删除文件
#define CMD_FILE_CLOSE      0x36  //   是否允许更新   空空空        关闭当前已经打开的文件或目录
#define CMD_DIR_INFO_READ   0x37  //   目录索引号     空空空        读取文件的目录信息 
#define CMD_DIR_INFO_SAVE   0x38  //   空空空空       空空空        保存文件的目录信息
#define CMD_BYTE_LOCATE     0x39  //   偏移字节数     空空空        以字节为单位移动当前文件指针
#define CMD_BYTE_READ       0x3a  //   请求字节数     空空空        以字节为单位从当前位置读取数据块
#define CMD_BYTE_RD_GO      0x3b  //   空空空空       空空空        继续字节读 
#define CMD_BYTE_WRITE      0x3c  //   请求字节数     空空空        以字节为单位向当前位置写入数据块
#define CMD_BYTE_WR_GO      0x3d  //   空空空空       空空空        继续字节写
#define CMD_DISK_CAPACITY   0x3e  //   空空空空       空空空        查询磁盘物理容量
#define CMD_DISK_QUERY      0x3f  //   空空空空       空空空        查询磁盘空间信息
#define CMD_DIR_CREATE      0x40  //   空空空空       空空空        新建目录并打开或打开已存在的目录
#define CMD_SEC_LOCATE      0x4a  //   偏移扇区数     空空空        以扇区为单位移动当前文件指针
#define CMD_SEC_READ        0x4b  //   请求扇区数     空空空        以扇区为单位从当前位置读取数据块 
#define CMD_SEC_WRITE       0x4c  //   请求扇区数     空空空        以扇区为单位在当前位置写入数据块 
#define CMD_DISK_BOC_CMD    0x50  //   空空空空       空空空        对USB存储器执行BO传输协议的命令
#define CMD_DISK_READ       0x54  //   LBA扇区地址    空空空        从USB存储器读物理扇区
#define CMD_DISK_RD_GO      0x55  //   空空空空       空空空        继续USB存储器的物理扇区读操作
#define CMD_DISK_WRITE      0x56  //   LBA扇区地址    空空空        向USB存储器写物理扇区 
#define CMD_DISK_WR_GO      0x57  //   空空空空       空空空        继续USB存储器的物理扇区写操作
#define CMD_RET_SUCCESS     0x51  //   状态代码       空空空        操作成功 
#define CMD_RET_ABORT       0x5f  //   状态代码       空空空        操作失败 
// ==================== 状态代码 ====================

#define USB_INT_SUCCESS     0x14  //   SD卡或者USB事务或者传输操作或者文件操作成功   
#define USB_INT_CONNECT     0x15  //   检测到USB设备连接事件
#define USB_INT_DISCONNECT  0x16  //   检测到USB设备断开事件
#define USB_INT_BUF_OVER    0x17  //   传输的数据有误或者数据太多缓冲区溢出
#define USB_INT_USB_READY   0x18  //   USB设备已经被初始化（已经分配USB地址）
#define USB_INT_DISK_READ   0x1d  //   存储设备读操作，请求数据读出
#define USB_INT_DISK_WRITE  0x1e  //   存储设备写操作，请求数据写入
#define USB_INT_DISK_ERR    0x1f  //   存储设备操作失败 
// ==================== 函数声明 ====================


//=============================================================================
// 初始化
//=============================================================================
void CH376_PORT_INIT(void);
//=============================================================================
// 底层读写操作
//=============================================================================
void ch376_spi_OutByte(uint8_t d);
uint8_t ch376_spi_InByte(void);
void CH376_Cmd_End(void);
void CH376_Cmd(uint8_t mCmd);
void CH376_Write(uint8_t mData);
uint8_t CH376_Read(void);
//=============================================================================
// 中断查询
//=============================================================================
uint8_t wait(void);
uint8_t CH376_GetStatus(void);
//=============================================================================
// CH376T 命令函数
//=============================================================================
// 获取芯片版本号0x01
uint8_t CH376T_Get_IC_Ver(void);

// 设置串口波特率0x02
// 需要在函数内部设置发送字节以调整波特率参数
uint8_t CH376T_SetBaudrate(void);//未验证

// 进入低功耗睡眠挂起状态0x03
void CH376T_EnterSleep(void);//未验证

// 执行硬件复位0x05
void CH376T_ResetAll(void);

// 检查芯片是否存在测试通讯接口和工作状态 0x06
uint8_t CH376T_CheckExist(void);//成功返回1，失败返回0

// 设置SPI和SDO的中断方式 0x0b
void CH376T_SetSdoInt(void);//未验证

// 获取当前文件长度（返回 4 字节）0x0c
uint32_t CH376T_GetFileSize(void);//未验证

// 设置 USB 模式 0x15
// 需要在函数内部设置发送字节以调整USB模式参数
uint8_t CH376T_SetUsbMode(void);

// 检测USB连接状态 0x30
uint8_t CH376T_CMD_DISK_CONNECT(void);//成功返回1，失败返回0

// 检测磁盘在线状态 0x30（返回【原始中断状态码】，用于识别介质拔插）
//   0x14 USB_INT_SUCCESS    → 磁盘在位
//   0x15 USB_INT_CONNECT    → 刚检测到USB设备连接(介质插回, 必须重新 MOUNT)
//   0x16 USB_INT_DISCONNECT → 检测到USB设备断开(介质拔出)
// 注意：会打断芯片当前的文件上下文，必须在"没有文件打开"时调用。
uint8_t CH376T_DiskCheckRaw(void);

// 初始化磁盘并测试磁盘是否就绪 0x31
uint8_t CH376T_DISK_MOUNT(void);//成功返回1，失败返回0

//打开文件0x2f  0x32
uint8_t CH376T_SET_FILE_NAME(void);//成功返回1，失败返回0

//创建文件0x2f  0x34
uint8_t CH376T_FILE_CREATE(void);//成功返回1，失败返回0

// 打开指定文件(名可为 "XXX.TXT" 或 "/XXX.TXT") 0x32
uint8_t CH376T_FILE_OPEN_NAME(const char *name);//成功返回1
uint8_t CH376T_FILE_OPEN_STATUS(const char *name);//返回原始状态码；0x14 成功，0x42 文件不存在
// 创建(覆盖)指定文件 0x34
uint8_t CH376T_FILE_CREATE_NAME(const char *name);//成功返回1

//向文件中写入数据 0x3c 0x2d 0x3d
uint8_t CH376T_BYTE_WRITE(void);//成功返回1，失败返回0

//向文件中写入任意数据(支持分批)
uint8_t CH376T_BYTE_WRITE_DATA(const uint8_t *data, uint16_t len);//成功返回1

//关闭文件 0x36
uint8_t CH376T_FILE_CLOSE(void);//成功返回1，失败返回0

//以字节为单位移动当前文件指针 0x39（追加写前先定位到文件末尾）
uint8_t CH376T_BYTE_LOCATE(uint32_t offset);//成功返回1，失败返回0

//读文件 0x3a 0x27 0x3b
uint8_t CH376T_BYTE_READ(void);//成功返回1，失败返回0





#endif // 说明：CH376_SPI_H
