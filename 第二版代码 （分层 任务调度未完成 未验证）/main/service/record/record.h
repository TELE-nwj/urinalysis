#ifndef SERVICE_RECORD_H
#define SERVICE_RECORD_H

/*
 * service 层：测量记录器（按“分钟”缓存本次测量的记录）。
 *
 * 【作用】保存一次测量的逐分钟记录数组（每条 = 时间戳 + 5 个数据），
 * 复位时（或暂停态手动）把整批记录一次性写入 U盘（经 service/storage）。
 * 数组元素为 rec_rec_t 结构体，record_* 提供增/查/清/存。
 *
 * 【依赖】record 依赖 service/storage 完成落盘；不依赖 app 层。
 */

#include "bsp/err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 单条记录 = 时间戳 + 5 个数据（对应串口屏显示的那 5 个量） */
typedef struct {
    uint32_t minute;      /* 时间戳：相对本次测量开始的第几分钟 */
    float    ml;          /* 1. 累计尿量 ml */
    float    rate_mlh;    /* 2. 实时速率 ml/h */
    uint32_t drops;       /* 3. 累计滴数 */
    char     status[16];  /* 4. 状态/报警文本(READY/LOW URINE/BAG FULL/...) */
    /* (第5个"计时"即上面的 minute 时间戳) */
} rec_rec_t;

/* 数组容量(条)。按每分钟一条，480 条 ≈ 8 小时；满了不再追加。 */
#define RECORD_MAX   480

/* 初始化(清空) */
void record_init(void);

/* 清空缓存(复位/开始新测量) */
void record_reset(void);

/* 追加一条(按分钟)。满则返回 ERR_NO_MEM。 */
err_t record_push(const rec_rec_t *r);

/* 当前缓存条数 */
uint16_t record_count(void);

/* 取第 i 条(越界返回 NULL) */
const rec_rec_t *record_get(uint16_t i);

/*
 * 把缓存整批写入 U盘(经 storage_write_text)。
 * 会把全部记录拼成多行文本一次写入；成功后【不清空】(由调用方决定 record_reset)。
 * 返回写入的条数；未就绪/失败返回 ERR_*。
 */
err_t record_save(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_RECORD_H */
