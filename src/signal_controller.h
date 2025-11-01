#ifndef SIGNAL_CONTROLLER_H
#define SIGNAL_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t frequency_hz;
    uint8_t drive_ma;
} signal_state_t;

bool signal_controller_init(void);
bool signal_controller_set(uint64_t frequency_hz, uint8_t drive_strength_ma);
signal_state_t signal_controller_get_state(void);

#endif // SIGNAL_CONTROLLER_H
