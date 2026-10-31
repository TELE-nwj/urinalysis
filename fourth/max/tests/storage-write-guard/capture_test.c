#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "driver/drop/drop.h"
static int irq_enabled, resets;
void *memset(void *p,int value,size_t n){unsigned char*s=p;while(n--)*s++=(unsigned char)value;return p;}
void test_log(const char*t,const char*f,...){(void)t;(void)f;}
err_t drop_init(void){irq_enabled=1;return ERR_OK;}
void drop_irq_disable(void){irq_enabled=0;}
void drop_irq_enable(void){irq_enabled=1;}
void drop_reset_interval(void){resets++;}
uint32_t drop_total(void){return 10;}
void drop_poll(void){}
uint64_t drop_last_interval_us(void){return 1000000;}
uint64_t drop_last_time_us(void){return 1000000;}
uint8_t drop_window_done(void){return 0;}
uint8_t drop_interval_sec(uint16_t n){(void)n;return 1;}
int64_t esp_timer_get_time(void){return 2000000;}
#include "service/urine/urine.c"
int test_capture_guard_keeps_pending_intervals(void){urine_capture_init();urine_capture_arm();int count=resets;bool prior=urine_capture_suspend();if(!prior||irq_enabled)return 1;urine_capture_restore(prior);return !irq_enabled||resets!=count;}
int test_capture_guard_preserves_paused_state(void){urine_capture_init();urine_capture_hold();int count=resets;bool prior=urine_capture_suspend();if(prior||irq_enabled)return 2;urine_capture_restore(prior);return irq_enabled||resets!=count;}
int test_capture_guard_preserves_initial_idle(void){bool prior=urine_capture_suspend();urine_capture_restore(prior);return prior||irq_enabled||resets;}
