#ifndef MORSE_PLAYER_H
#define MORSE_PLAYER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define MORSE_MAX_CHARS 20

typedef enum {
    MORSE_STATUS_IDLE = 0,
    MORSE_STATUS_PLAYING,
    MORSE_STATUS_STOPPED
} morse_status_t;

bool morse_start(const char *text, uint8_t len, uint16_t wpm, int16_t farnsworth_wpm);
void morse_stop(void);
bool morse_is_playing(void);
void morse_tick(void);

const char *morse_status_text(void);
const char *morse_last_error(void);
void morse_get_form_defaults(char *text_out, size_t text_len, uint16_t *wpm_out, int16_t *fwpm_out);

#endif // MORSE_PLAYER_H
