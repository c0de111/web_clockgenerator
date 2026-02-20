#ifndef WEBSERVER_PAGES_H
#define WEBSERVER_PAGES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void webserver_build_landing_page(char *buffer, size_t max_len, uint64_t frequency_hz,
                                  uint8_t drive_ma, bool output_enabled, const char *status_message,
                                  bool is_error, const char *morse_text, uint16_t morse_wpm,
                                  int16_t morse_fwpm, bool morse_playing, const char *morse_status,
                                  bool morse_hold_active);

#endif // WEBSERVER_PAGES_H
