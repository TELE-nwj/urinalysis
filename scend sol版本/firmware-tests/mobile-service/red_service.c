#include "service/mobile/mobile.h"
err_t service_mobile_start(const service_mobile_config_t *c) {(void)c;return ERR_NOT_SUP;}
void service_mobile_stop(void) {}
void service_mobile_tick(void) {}
void service_mobile_complete(uint64_t t,service_mobile_result_t r,service_mobile_state_t s,uint64_t q)
{(void)t;(void)r;(void)s;(void)q;}
