#include "morse_player.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "pico/time.h"

#include "logging.h"
#include "signal_controller.h"

#define MORSE_MAX_EVENTS 512

typedef struct {
    bool key_on;
    uint16_t duration_ms;
} morse_event_t;

typedef struct {
    const char *pattern;
    uint8_t length;
    bool word_gap_after;
} morse_char_entry_t;

typedef struct {
    bool playing;
    bool cancelled;
    uint8_t event_index;
    uint8_t event_count;
    morse_event_t events[MORSE_MAX_EVENTS];
    absolute_time_t next_deadline;
    uint16_t unit_ms;
    uint16_t gap_unit_ms;
    morse_status_t status;
    char last_text[MORSE_MAX_CHARS + 1];
    uint16_t last_wpm;
    int16_t last_fwpm;
    char error_msg[64];
} morse_state_t;

typedef struct {
    char symbol;
    const char *pattern;
} morse_map_entry_t;

static const morse_map_entry_t k_morse_map[] = {
    {'A', ".-"},    {'B', "-..."},   {'C', "-.-."},   {'D', "-.."},    {'E', "."},
    {'F', "..-."},  {'G', "--."},    {'H', "...."},   {'I', ".."},     {'J', ".---"},
    {'K', "-.-"},   {'L', ".-.."},   {'M', "--"},     {'N', "-."},     {'O', "---"},
    {'P', ".--."},  {'Q', "--.-"},   {'R', ".-."},    {'S', "..."},    {'T', "-"},
    {'U', "..-"},   {'V', "...-"},   {'W', ".--"},    {'X', "-..-"},   {'Y', "-.--"},
    {'Z', "--.."},  {'0', "-----"},  {'1', ".----"},  {'2', "..---"},  {'3', "...--"},
    {'4', "....-"}, {'5', "....."},  {'6', "-...."},  {'7', "--..."},  {'8', "---.."},
    {'9', "----."}, {'.', ".-.-.-"}, {',', "--..--"}, {'?', "..--.."}, {'/', "-..-."},
    {'=', "-...-"}, {'+', ".-.-."},  {'-', "-....-"}, {'!', "-.-.--"}, {'@', ".--.-."}};

static morse_state_t g_morse = {
    .playing = false,
    .cancelled = false,
    .event_index = 0,
    .event_count = 0,
    .next_deadline = {0},
    .unit_ms = 80,
    .gap_unit_ms = 80,
    .status = MORSE_STATUS_IDLE,
    .last_text = "PARIS",
    .last_wpm = 15,
    .last_fwpm = -1,
    .error_msg = {0},
};

static const morse_map_entry_t *lookup_symbol(char symbol) {
    for (size_t i = 0; i < sizeof(k_morse_map) / sizeof(k_morse_map[0]); ++i) {
        if (k_morse_map[i].symbol == symbol) {
            return &k_morse_map[i];
        }
    }
    return NULL;
}

static void reset_playback(bool cancelled) {
    if (g_morse.playing) {
        signal_controller_key(false);
        signal_controller_restore_output();
    }
    g_morse.playing = false;
    g_morse.cancelled = false;
    g_morse.event_index = 0;
    g_morse.event_count = 0;
    g_morse.next_deadline = get_absolute_time();
    g_morse.status = cancelled ? MORSE_STATUS_STOPPED : MORSE_STATUS_IDLE;
}

static bool build_events(const morse_char_entry_t *entries, uint8_t count) {
    g_morse.event_count = 0;

    for (uint8_t i = 0; i < count; ++i) {
        const morse_char_entry_t *entry = &entries[i];
        if (!entry->pattern || entry->length == 0) {
            continue;
        }
        for (uint8_t j = 0; j < entry->length; ++j) {
            if (g_morse.event_count >= MORSE_MAX_EVENTS - 1) {
                snprintf(g_morse.error_msg, sizeof(g_morse.error_msg), "Message too long");
                return false;
            }
            bool is_dash = (entry->pattern[j] == '-');
            uint16_t on_duration = g_morse.unit_ms * (is_dash ? 3u : 1u);
            g_morse.events[g_morse.event_count++] = (morse_event_t){true, on_duration};

            bool last_symbol = (j == entry->length - 1);
            uint16_t off_duration = g_morse.unit_ms;
            if (last_symbol) {
                if (entry->word_gap_after) {
                    off_duration = g_morse.gap_unit_ms * 7u;
                } else if (i == count - 1) {
                    off_duration = 0;
                } else {
                    off_duration = g_morse.gap_unit_ms * 3u;
                }
            }
            g_morse.events[g_morse.event_count++] = (morse_event_t){false, off_duration};
        }
    }

    return g_morse.event_count > 0;
}

bool morse_start(const char *text, uint8_t len, uint16_t wpm, int16_t farnsworth_wpm) {
    if (!text) {
        return false;
    }
    if (g_morse.playing) {
        snprintf(g_morse.error_msg, sizeof(g_morse.error_msg), "Busy");
        return false;
    }
    if (len == 0 || len > MORSE_MAX_CHARS) {
        snprintf(g_morse.error_msg, sizeof(g_morse.error_msg), "Text must be 1-%u characters",
                 (unsigned)MORSE_MAX_CHARS);
        return false;
    }
    if (wpm < 1 || wpm > 1000) {
        snprintf(g_morse.error_msg, sizeof(g_morse.error_msg), "WPM must be 1-1000");
        return false;
    }
    if (farnsworth_wpm >= 0) {
        if (farnsworth_wpm < 1 || (uint16_t)farnsworth_wpm > wpm) {
            snprintf(g_morse.error_msg, sizeof(g_morse.error_msg), "Farnsworth must be 1-%u",
                     (unsigned)wpm);
            return false;
        }
    }

    g_morse.error_msg[0] = '\0';

    size_t input_len = strnlen(text, len);
    if (input_len == 0) {
        snprintf(g_morse.error_msg, sizeof(g_morse.error_msg), "Text must be 1-%u characters",
                 (unsigned)MORSE_MAX_CHARS);
        return false;
    }

    size_t copy_len = input_len < MORSE_MAX_CHARS ? input_len : MORSE_MAX_CHARS;
    memcpy(g_morse.last_text, text, copy_len);
    g_morse.last_text[copy_len] = '\0';

    g_morse.last_wpm = wpm;
    g_morse.last_fwpm = farnsworth_wpm;

    morse_char_entry_t entries[MORSE_MAX_CHARS] = {0};
    uint8_t entry_count = 0;
    char invalid_chars[MORSE_MAX_CHARS + 1] = {0};
    size_t invalid_count = 0;

    for (size_t i = 0; i < input_len; ++i) {
        char c = text[i];
        if (c == '\0') {
            break;
        }
        if (c == ' ') {
            if (entry_count > 0) {
                entries[entry_count - 1].word_gap_after = true;
            }
            continue;
        }
        if (entry_count >= MORSE_MAX_CHARS) {
            break;
        }
        char upper = (char)toupper((unsigned char)c);
        const morse_map_entry_t *mapped = lookup_symbol(upper);
        if (!mapped) {
            if (invalid_count < sizeof(invalid_chars) - 1) {
                invalid_chars[invalid_count++] = c;
            }
            continue;
        }
        entries[entry_count].pattern = mapped->pattern;
        entries[entry_count].length = (uint8_t)strlen(mapped->pattern);
        entries[entry_count].word_gap_after = false;
        entry_count++;
    }

    if (invalid_count > 0) {
        snprintf(g_morse.error_msg, sizeof(g_morse.error_msg), "Invalid characters: %s",
                 invalid_chars);
        return false;
    }
    if (entry_count == 0) {
        snprintf(g_morse.error_msg, sizeof(g_morse.error_msg), "Message has no valid characters");
        return false;
    }

    g_morse.unit_ms = (uint16_t)(1200u / wpm);
    if (g_morse.unit_ms == 0) {
        g_morse.unit_ms = 1;
    }
    int8_t effective_fw = -1;
    if (farnsworth_wpm >= 1 && (uint16_t)farnsworth_wpm < wpm) {
        g_morse.gap_unit_ms = (uint16_t)(1200u / (uint16_t)farnsworth_wpm);
        effective_fw = farnsworth_wpm;
    } else {
        g_morse.gap_unit_ms = g_morse.unit_ms;
    }

    if (!build_events(entries, entry_count)) {
        if (g_morse.error_msg[0] == '\0') {
            snprintf(g_morse.error_msg, sizeof(g_morse.error_msg),
                     "Failed to build Morse sequence");
        }
        return false;
    }

    if (!signal_controller_key(false)) {
        snprintf(g_morse.error_msg, sizeof(g_morse.error_msg), "Output not initialized");
        return false;
    }

    g_morse.playing = true;
    g_morse.cancelled = false;
    g_morse.event_index = 0;
    g_morse.next_deadline = get_absolute_time();
    g_morse.status = MORSE_STATUS_PLAYING;
    g_morse.last_wpm = wpm;
    g_morse.last_fwpm = effective_fw;
    memset(g_morse.error_msg, 0, sizeof(g_morse.error_msg));

    uint32_t total_ms = 0;
    for (uint8_t i = 0; i < g_morse.event_count; ++i) {
        total_ms += g_morse.events[i].duration_ms;
    }

    char fwpm_buf[16];
    if (effective_fw > 0) {
        snprintf(fwpm_buf, sizeof(fwpm_buf), "%d", effective_fw);
    } else {
        snprintf(fwpm_buf, sizeof(fwpm_buf), "off");
    }

    log_info("[MORSE] start text=\"%s\" wpm=%u fwpm=%s total_ms=%u", g_morse.last_text,
             (unsigned)wpm, fwpm_buf, (unsigned)total_ms);
    return true;
}

void morse_stop(void) {
    if (!g_morse.playing) {
        g_morse.status = MORSE_STATUS_STOPPED;
        return;
    }
    g_morse.cancelled = true;
    g_morse.next_deadline = get_absolute_time();
}

bool morse_is_playing(void) { return g_morse.playing; }

void morse_tick(void) {
    if (!g_morse.playing) {
        return;
    }

    if (!time_reached(g_morse.next_deadline)) {
        return;
    }

    if (g_morse.cancelled) {
        log_info("[MORSE] stopped");
        reset_playback(true);
        return;
    }

    if (g_morse.event_index >= g_morse.event_count) {
        log_info("[MORSE] done");
        reset_playback(false);
        return;
    }

    morse_event_t event = g_morse.events[g_morse.event_index++];
    signal_controller_key(event.key_on);

    if (event.duration_ms == 0) {
        g_morse.next_deadline = get_absolute_time();
    } else {
        g_morse.next_deadline = make_timeout_time_ms(event.duration_ms);
    }
}

const char *morse_status_text(void) {
    switch (g_morse.status) {
    case MORSE_STATUS_PLAYING:
        return "Playing...";
    case MORSE_STATUS_STOPPED:
        return "Stopped";
    case MORSE_STATUS_IDLE:
    default:
        return "Idle";
    }
}

const char *morse_last_error(void) { return g_morse.error_msg; }

void morse_get_form_defaults(char *text_out, size_t text_len, uint16_t *wpm_out,
                             int16_t *fwpm_out) {
    if (text_out && text_len > 0) {
        snprintf(text_out, text_len, "%s", g_morse.last_text);
    }
    if (wpm_out) {
        *wpm_out = g_morse.last_wpm;
    }
    if (fwpm_out) {
        *fwpm_out = g_morse.last_fwpm;
    }
}
