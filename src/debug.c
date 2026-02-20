#include "debug.h"

#include <stdarg.h>
#include <stdio.h>

#include "pico/stdlib.h"

static char debug_buffer[DEBUG_BUFFER_SIZE];
static size_t debug_buffer_index = 0;
static DebugMode current_debug_mode = DEBUG_NONE;

void init_debug(void) {
    debug_buffer_index = 0;
    current_debug_mode = DEBUG_NONE;
}

void set_debug_mode(DebugMode mode) { current_debug_mode = mode; }

static void append_to_buffer(const char *timestamp, const char *color, const char *message) {
    if (debug_buffer_index >= DEBUG_BUFFER_SIZE) {
        return;
    }

    int written = snprintf(&debug_buffer[debug_buffer_index],
                           DEBUG_BUFFER_SIZE - debug_buffer_index, "%s", timestamp);
    if (written > 0) {
        debug_buffer_index += (size_t)written;
    }

    if (color && *color) {
        written = snprintf(&debug_buffer[debug_buffer_index],
                           DEBUG_BUFFER_SIZE - debug_buffer_index, "%s", color);
        if (written > 0) {
            debug_buffer_index += (size_t)written;
        }
    }

    written = snprintf(&debug_buffer[debug_buffer_index], DEBUG_BUFFER_SIZE - debug_buffer_index,
                       "%s", message);
    if (written > 0) {
        debug_buffer_index += (size_t)written;
    }

    if (color && *color) {
        written = snprintf(&debug_buffer[debug_buffer_index],
                           DEBUG_BUFFER_SIZE - debug_buffer_index, "%s", COLOR_RESET);
        if (written > 0) {
            debug_buffer_index += (size_t)written;
        }
    }

    if (debug_buffer_index >= DEBUG_BUFFER_SIZE) {
        debug_buffer_index = DEBUG_BUFFER_SIZE - 1;
    }
}

static void format_timestamp(char *out, size_t out_len) {
    const uint64_t timestamp_us = time_us_64();
    if (timestamp_us < 10000) {
        snprintf(out, out_len, "\033[1m[%llu us]\033[0m ", (unsigned long long)timestamp_us);
    } else {
        snprintf(out, out_len, "\033[1m[%llu ms]\033[0m ",
                 (unsigned long long)(timestamp_us / 1000));
    }
}

void debug_log(const char *format, ...) {
    va_list args;
    va_start(args, format);

    char message[256];
    vsnprintf(message, sizeof(message), format, args);

    char timestamp[32];
    format_timestamp(timestamp, sizeof timestamp);

    if (current_debug_mode == DEBUG_REALTIME || current_debug_mode == DEBUG_BOTH) {
        printf("%s", timestamp);
        printf("%s", message);
    }

    if (current_debug_mode == DEBUG_BUFFERED || current_debug_mode == DEBUG_BOTH) {
        append_to_buffer(timestamp, NULL, message);
    }

    va_end(args);
}

void debug_log_with_color(const char *color_code, const char *format, ...) {
    va_list args;
    va_start(args, format);

    char message[256];
    vsnprintf(message, sizeof(message), format, args);

    char timestamp[32];
    format_timestamp(timestamp, sizeof timestamp);

    if (current_debug_mode == DEBUG_REALTIME || current_debug_mode == DEBUG_BOTH) {
        printf("%s", timestamp);
        printf("%s", color_code ? color_code : "");
        printf("%s", message);
        if (color_code && *color_code) {
            printf("%s", COLOR_RESET);
        }
    }

    if (current_debug_mode == DEBUG_BUFFERED || current_debug_mode == DEBUG_BOTH) {
        append_to_buffer(timestamp, color_code, message);
    }

    va_end(args);
}

void transmit_debug_logs(void) {
    if (current_debug_mode == DEBUG_BUFFERED || current_debug_mode == DEBUG_BOTH) {
        printf("%s", debug_buffer);
        debug_buffer_index = 0;
    }
}
