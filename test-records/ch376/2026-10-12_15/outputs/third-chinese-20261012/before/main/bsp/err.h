#ifndef BSP_ERR_H
#define BSP_ERR_H

/*
 * bsp 层：通用错误码/返回码，供各层函数统一返回。
 * (由原 common/err.h 移入 bsp)
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t err_t;

#define ERR_OK          0    /* 成功 */
#define ERR_FAIL       -1    /* 一般失败 */
#define ERR_TIMEOUT    -2    /* 超时 */
#define ERR_NOT_READY  -3    /* 未就绪 */
#define ERR_BUSY       -4    /* 忙 */
#define ERR_PARAM      -5    /* 参数错误 */
#define ERR_NO_MEM     -6    /* 内存不足 */
#define ERR_NOT_SUP    -7    /* 不支持/未实现 */

#ifdef __cplusplus
}
#endif

#endif /* BSP_ERR_H */
