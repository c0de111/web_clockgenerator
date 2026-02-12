#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <stdbool.h>

void webserver_init(void);
void webserver_set_status(const char *message, bool is_error);

#endif // WEBSERVER_H
