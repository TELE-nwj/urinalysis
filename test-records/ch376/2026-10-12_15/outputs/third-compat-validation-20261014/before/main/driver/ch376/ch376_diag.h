#ifndef CH376_DIAG_H
#define CH376_DIAG_H
#include <stdbool.h>
#include <stdint.h>
/* 仅日志测试副本：只读探针不修正地址，不写物理扇区或内部变量。 */
void CH376_DiagReset(void);
bool CH376_DiagPrepare(bool force_read_boot);
void CH376_DiagFile(const char *stage, const char *name);
uint32_t CH376_DiagLocateResult(uint32_t offset);
#endif
