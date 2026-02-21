#include "signal_controller.h"

#include "logging.h"
#include "si5351.h"

typedef struct {
    uint64_t frequency_hz;
    uint8_t drive_ma;
    bool output_enabled;
} signal_state_t;

static bool g_initialized = false;
static signal_state_t g_state = {
    .frequency_hz = 1008000,
    .drive_ma = 4,
    .output_enabled = false,
};

static enum si5351_drive map_drive(uint8_t drive_ma) {
    switch (drive_ma) {
    case 2:
        return SI5351_DRIVE_2MA;
    case 4:
        return SI5351_DRIVE_4MA;
    case 6:
        return SI5351_DRIVE_6MA;
    case 8:
        return SI5351_DRIVE_8MA;
    default:
        return SI5351_DRIVE_4MA;
    }
}

bool signal_controller_init(void) {
    if (g_initialized) {
        return true;
    }

    log_info("[SI5351] controller init requested");

    bool ok = si5351_init(SI5351_BUS_BASE_ADDR, SI5351_CRYSTAL_LOAD_8PF, SI5351_XTAL_FREQ, 0);
    if (!ok) {
        log_error("[SI5351] init failed");
        return false;
    }

    uint64_t scaled = g_state.frequency_hz * SI5351_FREQ_MULT;
    if (si5351_set_freq(scaled, SI5351_CLK0) != 0) {
        log_error("[SI5351] default frequency set failed");
        return false;
    }
    si5351_drive_strength(SI5351_CLK0, map_drive(g_state.drive_ma));
    si5351_output_enable(SI5351_CLK0, 0);

    g_initialized = true;
    log_info("[SI5351] initialized (freq=%llu Hz, drive=%u mA)",
             (unsigned long long)g_state.frequency_hz, g_state.drive_ma);
    return true;
}

bool signal_controller_set(uint64_t frequency_hz, uint8_t drive_strength_ma) {
    if (!g_initialized && !signal_controller_init()) {
        return false;
    }

    uint8_t drive = drive_strength_ma;
    if (drive != 2 && drive != 4 && drive != 6 && drive != 8) {
        drive = 4;
    }

    const bool freq_changed = (g_state.frequency_hz != frequency_hz);
    const bool drive_changed = (g_state.drive_ma != drive);

    if (freq_changed || drive_changed) {
        const uint64_t scaled = frequency_hz * SI5351_FREQ_MULT;
        if (si5351_set_freq(scaled, SI5351_CLK0) != 0) {
            log_error("[SI5351] failed to set frequency %llu Hz", (unsigned long long)frequency_hz);
            return false;
        }

        si5351_drive_strength(SI5351_CLK0, map_drive(drive));

        uint8_t ctrl_reg = si5351_read(SI5351_CLK0_CTRL);
        log_info("[SI5351] CLK0 control=0x%02X (requested %u mA)", ctrl_reg, drive);

        g_state.frequency_hz = frequency_hz;
        g_state.drive_ma = drive;

        log_info("[USER] freq=%llu Hz, drive=%u mA", (unsigned long long)frequency_hz, drive);
    }
    return true;
}

bool signal_controller_enable_output(bool enable) {
    if (!g_initialized && !signal_controller_init()) {
        return false;
    }

    si5351_output_enable(SI5351_CLK0, enable ? 1 : 0);
    if (g_state.output_enabled != enable) {
        g_state.output_enabled = enable;
        log_info("[USER] output=%s", enable ? "on" : "off");
    }
    return true;
}

bool signal_controller_key(bool on) {
    if (!g_initialized) {
        return false;
    }
    si5351_output_enable(SI5351_CLK0, on ? 1 : 0);
    return true;
}

void signal_controller_restore_output(void) {
    if (!g_initialized) {
        return;
    }
    si5351_output_enable(SI5351_CLK0, g_state.output_enabled ? 1 : 0);
}

uint64_t signal_controller_get_frequency_hz(void) { return g_state.frequency_hz; }

uint8_t signal_controller_get_drive_ma(void) { return g_state.drive_ma; }

bool signal_controller_is_output_enabled(void) { return g_state.output_enabled; }
