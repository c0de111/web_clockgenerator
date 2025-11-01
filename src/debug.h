#ifndef DEBUG_H
#define DEBUG_H

#include <stdint.h>

#define DEBUG_BUFFER_SIZE 4096

typedef enum {
    DEBUG_NONE,
    DEBUG_REALTIME,
    DEBUG_BUFFERED,
    DEBUG_BOTH
} DebugMode;

void init_debug(void);
void set_debug_mode(DebugMode mode);
void debug_log(const char* format, ...);
void debug_log_with_color(const char* color_code, const char* format, ...);
void transmit_debug_logs(void);

#define COLOR_RESET "\033[0m"
#define COLOR_RED "\033[31m"
#define COLOR_GREEN "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_BLUE "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN "\033[36m"
#define COLOR_WHITE "\033[37m"
#define COLOR_BOLD_RED "\033[1;31m"
#define COLOR_BOLD_GREEN "\033[1;32m"
#define COLOR_BOLD_YELLOW "\033[1;33m"
#define COLOR_BOLD_BLUE "\033[1;34m"
#define COLOR_BOLD_MAGENTA "\033[1;35m"
#define COLOR_BOLD_CYAN "\033[1;36m"
#define COLOR_BOLD_WHITE "\033[1;37m"

#endif // DEBUG_H
