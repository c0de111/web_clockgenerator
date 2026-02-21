#ifndef SIGNAL_CONTROLLER_H
#define SIGNAL_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

bool signal_controller_init(void);
bool signal_controller_set(uint64_t frequency_hz, uint8_t drive_strength_ma);
bool signal_controller_enable_output(bool enable);
bool signal_controller_key(bool on);
void signal_controller_restore_output(void);
uint64_t signal_controller_get_frequency_hz(void);
uint8_t signal_controller_get_drive_ma(void);
bool signal_controller_is_output_enabled(void);

#endif // SIGNAL_CONTROLLER_H
