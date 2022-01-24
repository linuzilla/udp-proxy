#pragma once

extern int trigger_by_udp(volatile bool *terminate, const char *server, const int port, void (*func)(const char *,const int, const char *, const int));
/* vim settings {{{
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 * }}} */
