#ifndef LOGGING_H
#define LOGGING_H

#include <stdarg.h>

void logging_init(void);
void logging_poll(void);
void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_error(const char *fmt, ...);

#endif // LOGGING_H
