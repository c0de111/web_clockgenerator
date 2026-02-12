#ifndef SIGNAL_CONTROLLER_H
#define SIGNAL_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t frequency_hz;
    uint8_t drive_ma;
    bool output_enabled;
} signal_state_t;

bool signal_controller_init(void);
bool signal_controller_set(uint64_t frequency_hz, uint8_t drive_strength_ma);
bool signal_controller_enable_output(bool enable);
bool signal_controller_key(bool on);
void signal_controller_restore_output(void);
signal_state_t signal_controller_get_state(void);

#endif // SIGNAL_CONTROLLER_H
