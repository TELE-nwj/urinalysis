#ifndef SERVICE_STORAGE_H
#define SERVICE_STORAGE_H

/* service 层：CH376 U 盘会话文件服务。文件每分钟打开、追加、关闭一次。 */

#include "bsp/err.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum { STORAGE_UNKNOWN, STORAGE_ABSENT, STORAGE_READY, STORAGE_ERROR } storage_state_t;
typedef struct {
    storage_state_t state;
    bool checked;
    uint64_t checked_uptime_ms;
} storage_status_t;
/* Cached observation only: never performs CH376 I/O. */
bool storage_get_status(storage_status_t *status);

#ifdef __cplusplus
extern "C" {
#endif

/* 完整文件名长度上限(用户名 + ".txt" + '\0') */
#define STORAGE_FILE_MAX   40u

/* ---- 记录文件的列布局（表头与数据行共用，改这里就两处一起变）----
 * 按等宽字体计算：ASCII 占 1 格，中文占 2 格。
 * 表头与数据都【左对齐】：数据的第一个字符就在该列表头第一个字的下方。 */
#define REC_COL1_W   14         /* 列1 内容宽: "时间(hh:mm:ss)" / "00:01:00" */
#define REC_COL2_W   12         /* 列2 内容宽: "尿量(ml)"       / "12.3ml"   */
                                /*  (在原来 8 格基础上再加 4 格)             */
#define REC_GAP1      "    "    /* 列1 → 列2 间隔: 4 空格 */
#define REC_GAP2      "    "    /* 列2 → 列3 间隔: 4 空格 (叠在列2加宽之后) */
#define REC_TAIL      "  "      /* 行尾补白: 2 空格 */

/* 把宏值转成字符串，用来拼出格式串(避免列宽改了两边不同步) */
#define REC_STR_(x)   #x
#define REC_STR(x)    REC_STR_(x)

/* 数据行格式串: 左对齐 + 列间间隔 + 行尾补白，与上面几个宏严格一致 */
#define REC_ROW_FMT \
    "%-" REC_STR(REC_COL1_W) "s" REC_GAP1 \
    "%-" REC_STR(REC_COL2_W) "s" REC_GAP2 \
    "%s" REC_TAIL

/* 文件表头：创建文件时写第一行
 * 对齐效果(等宽字体)：
 *   时间(hh:mm:ss)    尿量(ml)            平均流速(ml/h)
 *   00:01:00          1.6ml               96.0ml/h */
#define STORAGE_HEADER \
    "时间(hh:mm:ss)"  REC_GAP1 \
    "尿量(ml)"        REC_GAP2 \
    "平均流速(ml/h)"  REC_TAIL "\r\n"

/* 初始化存储：CH376 SPI + 进主机模式 + 检测磁盘 */
err_t storage_init(void);

/* 在 U 盘上分配新的 /U0000001.TXT ... /U9999999.TXT。
 * stem 接收不带扩展名的 8 字符编号，缓冲区至少 9 字节。 */
err_t storage_create_session(char *stem, size_t stem_size);

/* 断盘后恢复本次会话文件；不存在则建文件及表头。 */
err_t storage_ensure_session(const char *stem);

/* 打开本次会话文件并定位末尾，供当分钟追加。 */
err_t storage_open_session(const char *stem);

/* 往"当前打开的文件"续写文本(文件保持打开，可多次调用) */
err_t storage_write_open(const char *text);

/* 关闭当前打开的文件并清状态(暂停/复位时调用) */
err_t storage_close_session(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_STORAGE_H */
