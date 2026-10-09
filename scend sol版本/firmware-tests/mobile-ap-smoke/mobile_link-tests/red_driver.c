/* Deliberately missing implementation, retained to reproduce the initial RED. */
#include "mobile_link.h"
err_t mobile_link_start(const mobile_link_config_t *c, const mobile_link_callbacks_t *b)
{ (void)c; (void)b; return ERR_NOT_SUP; }
err_t mobile_link_send_text(mobile_link_peer_t p,const char *s,size_t n)
{ (void)p; (void)s; (void)n; return ERR_NOT_SUP; }
void mobile_link_stop(void) {}
