#include "storage/settings_store.h"
#include <Preferences.h>

namespace {
constexpr char kNs[] = "eweed_hmi";

void putFloat(Preferences &p, const char *key, float v) { p.putFloat(key, v); }
float getFloat(Preferences &p, const char *key, float d) { return p.getFloat(key, d); }
void putUInt(Preferences &p, const char *key, uint32_t v) { p.putUInt(key, v); }
uint32_t getUInt(Preferences &p, const char *key, uint32_t d) { return p.getUInt(key, d); }
void putChar(Preferences &p, const char *key, int8_t v) { p.putChar(key, v); }
int8_t getChar(Preferences &p, const char *key, int8_t d) { return p.getChar(key, d); }
}  // namespace

void settingsLoadInto(SystemSettings &s) {
    Preferences p;
    if (!p.begin(kNs, true)) {
        return;
    }

    s.dark_theme            = p.getBool("dark", s.dark_theme);
    s.sound_enabled         = p.getBool("sound", s.sound_enabled);
    s.safety_ppm_ceiling    = static_cast<uint16_t>(p.getUInt("saf_ppm", s.safety_ppm_ceiling));
    s.safety_ph_ceiling     = getFloat(p, "saf_ph", s.safety_ph_ceiling);
    s.recirc_on_sec         = getUInt(p, "rec_on", s.recirc_on_sec);
    s.recirc_off_sec        = getUInt(p, "rec_off", s.recirc_off_sec);
    s.stabilize_sec         = getUInt(p, "stab", s.stabilize_sec);
    s.microdose_ml          = getFloat(p, "micro", s.microdose_ml);
    s.photo_on_hour         = static_cast<uint8_t>(p.getUChar("ph_on", s.photo_on_hour));
    s.photo_hours           = static_cast<uint8_t>(p.getUChar("ph_h", s.photo_hours));
    s.vent_temp_on_c        = getFloat(p, "v_t_on", s.vent_temp_on_c);
    s.vent_temp_off_c       = getFloat(p, "v_t_off", s.vent_temp_off_c);
    s.uart_baud             = getUInt(p, "u_baud", s.uart_baud);
    s.uart_rx_pin           = getChar(p, "u_rx", s.uart_rx_pin);
    s.uart_tx_pin           = getChar(p, "u_tx", s.uart_tx_pin);
    s.brightness_pct        = static_cast<uint8_t>(p.getUChar("bright", s.brightness_pct));
    s.ui_idle_timeout_ms    = getUInt(p, "idle_ms", s.ui_idle_timeout_ms);
    s.ph_slope              = getFloat(p, "ph_slope", s.ph_slope);
    s.tds_cal_factor        = getFloat(p, "tds_fact", s.tds_cal_factor);

    for (int i = 0; i < 5; ++i) {
        char k[16];
        snprintf(k, sizeof(k), "ph_t%d", i);
        s.ph_target[i] = getFloat(p, k, s.ph_target[i]);
        snprintf(k, sizeof(k), "tds_t%d", i);
        s.tds_target[i] = static_cast<int>(p.getInt(k, s.tds_target[i]));
    }

    p.end();
}

void settingsSaveFrom(const SystemSettings &s) {
    Preferences p;
    if (!p.begin(kNs, false)) {
        return;
    }

    p.putBool("dark", s.dark_theme);
    p.putBool("sound", s.sound_enabled);
    p.putUInt("saf_ppm", s.safety_ppm_ceiling);
    putFloat(p, "saf_ph", s.safety_ph_ceiling);
    putUInt(p, "rec_on", s.recirc_on_sec);
    putUInt(p, "rec_off", s.recirc_off_sec);
    putUInt(p, "stab", s.stabilize_sec);
    putFloat(p, "micro", s.microdose_ml);
    p.putUChar("ph_on", s.photo_on_hour);
    p.putUChar("ph_h", s.photo_hours);
    putFloat(p, "v_t_on", s.vent_temp_on_c);
    putFloat(p, "v_t_off", s.vent_temp_off_c);
    putUInt(p, "u_baud", s.uart_baud);
    putChar(p, "u_rx", s.uart_rx_pin);
    putChar(p, "u_tx", s.uart_tx_pin);
    p.putUChar("bright", s.brightness_pct);
    putUInt(p, "idle_ms", s.ui_idle_timeout_ms);
    putFloat(p, "ph_slope", s.ph_slope);
    putFloat(p, "tds_fact", s.tds_cal_factor);

    for (int i = 0; i < 5; ++i) {
        char k[16];
        snprintf(k, sizeof(k), "ph_t%d", i);
        putFloat(p, k, s.ph_target[i]);
        snprintf(k, sizeof(k), "tds_t%d", i);
        p.putInt(k, s.tds_target[i]);
    }

    p.end();
}
