#ifndef WEBSERVER_UTILS_H
#define WEBSERVER_UTILS_H

#include "lwip/err.h"
#include "lwip/tcp.h"

err_t webserver_send_response(struct tcp_pcb *pcb, const char *body);

#endif // WEBSERVER_UTILS_H
