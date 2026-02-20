#include "logging.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"

void logging_init(void) {
    init_debug();
    set_debug_mode(DEBUG_REALTIME);
}

static void log_vinternal(const char *label, const char *color, const char *fmt, va_list args) {
    char message[256];
    vsnprintf(message, sizeof(message), fmt, args);

    size_t len = strlen(message);
    if (len > 0 && message[len - 1] == '\n') {
        message[len - 1] = '\0';
    }

    if (color) {
        debug_log_with_color(color, "%s%s\n", label, message);
    } else {
        debug_log("%s%s\n", label, message);
    }
}

void log_info(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_vinternal("[INFO] ", NULL, fmt, args);
    va_end(args);
}

void log_warn(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_vinternal("[WARN] ", COLOR_BOLD_YELLOW, fmt, args);
    va_end(args);
}

void log_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_vinternal("[ERROR] ", COLOR_BOLD_RED, fmt, args);
    va_end(args);
}
