#ifndef DRIVER_BATTERY_H
#define DRIVER_BATTERY_H

/* driver 层：电池电量接口。暂为占位。 */

#include "bsp/err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

err_t battery_init(void);
err_t battery_read_percent(int *percent);  /* 0~100 */
err_t battery_low_flag(void);              /* 是否低电量 */

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_BATTERY_H */
