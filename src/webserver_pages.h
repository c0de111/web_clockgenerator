#ifndef WEBSERVER_PAGES_H
#define WEBSERVER_PAGES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

void webserver_build_landing_page(char *buffer, size_t max_len, uint64_t frequency_hz,
                                  uint8_t drive_ma, bool output_enabled,
                                  const char *status_message, bool is_error);

#endif // WEBSERVER_PAGES_H
