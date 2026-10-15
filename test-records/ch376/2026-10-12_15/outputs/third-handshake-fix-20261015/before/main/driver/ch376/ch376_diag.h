#ifndef CH376_DIAG_H
#define CH376_DIAG_H
#include <stdbool.h>
#include <stdint.h>
/* 测试副本：物理探针只读；旧版本兼容只按厂商流程写芯片内部变量。 */
void CH376_DiagReset(void);
bool CH376_DiagPrepare(bool force_read_boot);
void CH376_DiagFile(const char *stage, const char *name);
uint32_t CH376_DiagLocateResult(uint32_t offset);
bool CH376_DiagEnsureReady(void);
bool CH376_DiagCanWrite(const char *stage);
bool CH376_DiagCanFeed(const char *stage);
bool CH376_DiagWriteFault(void);
bool CH376_DiagCanFinish(void);
void CH376_DiagRootPath(const char *name);
bool CH376_DiagVerifyFile(const char *name,uint32_t size,uint32_t offset,const char *text,uint16_t length);
#endif
