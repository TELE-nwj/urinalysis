#ifndef SERVICE_STORAGE_H
#define SERVICE_STORAGE_H

/* service 层：U盘存储服务。
 * 对上层提供"把一条文本记录写入U盘文件"的能力，屏蔽 CH376 细节。 */

#include "bsp/err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化存储：CH376 SPI + 进主机模式 + 检测磁盘 */
err_t storage_init(void);

/* 向 U 盘写入一条文本记录(覆盖式, 文件名 storage 内部定义) */
err_t storage_write_text(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_STORAGE_H */
