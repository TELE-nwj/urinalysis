#include "record.h"
#include "service/storage/storage.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "record";

/* 本层静态数组：缓存一次测量的所有分钟记录 */
static rec_rec_t s_recs[RECORD_MAX];
static uint16_t  s_cnt = 0;

void record_init(void)
{
    s_cnt = 0;
    memset(s_recs, 0, sizeof(s_recs));
}

void record_reset(void)
{
    s_cnt = 0;
}

err_t record_push(const rec_rec_t *r)
{
    if (!r) return ERR_PARAM;
    if (s_cnt >= RECORD_MAX) {
        ESP_LOGW(TAG, "记录缓存已满(%d), 丢弃本条", (int)RECORD_MAX);
        return ERR_NO_MEM;
    }
    s_recs[s_cnt++] = *r;   /* 结构体整体拷贝 */
    return ERR_OK;
}

uint16_t record_count(void)
{
    return s_cnt;
}

const rec_rec_t *record_get(uint16_t i)
{
    return (i < s_cnt) ? &s_recs[i] : NULL;
}

err_t record_save(void)
{
    if (s_cnt == 0) {
        ESP_LOGI(TAG, "无记录可保存");
        return ERR_OK;
    }

    /* 把全部记录拼成一段多行文本，一次性写入 U盘。
       每行约 60 字节，为安全开 80 字节/条。 */
    size_t need = (size_t)s_cnt * 80 + 2;
    char *buf = (char *)malloc(need);
    if (!buf) return ERR_NO_MEM;

    char *p = buf;
    size_t left = need;
    for (uint16_t i = 0; i < s_cnt; i++) {
        const rec_rec_t *r = &s_recs[i];
        int n = snprintf(p, left,
                         "min=%lu ml=%.1f rate=%.1f drops=%lu status=%s\n",
                         (unsigned long)r->minute,
                         (double)r->ml,
                         (double)r->rate_mlh,
                         (unsigned long)r->drops,
                         r->status);
        if (n < 0) { free(buf); return ERR_FAIL; }
        p    += n;
        left -= (size_t)n;
    }

    err_t ret = storage_write_text(buf);
    free(buf);

    if (ret != ERR_OK) {
        ESP_LOGE(TAG, "保存到U盘失败");
        return ret;
    }
    ESP_LOGI(TAG, "已保存 %u 条记录到 U盘", (unsigned)s_cnt);
    return ERR_OK;
}
