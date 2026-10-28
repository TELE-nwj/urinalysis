#pragma once
/* Mirror the real SDK's conflicting namespace, not BSP's int32_t API. */
typedef signed char err_t;
enum { ERR_OK = 0, ERR_MEM = -1 };
