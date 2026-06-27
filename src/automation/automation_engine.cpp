#include "automation/automation_engine.h"
#include "comms/uart_comm.h"

void AutomationEngine::sendActuatorCmd(const char *key, bool on) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "CMD:OUT:%s:%d", key, on ? 1 : 0);
    UartComm::getInstance().sendCommand(cmd);
}

void AutomationEngine::evaluate() {
    const uint32_t now = millis();

    DataModel::getInstance().lock();
    SystemState    state    = DataModel::getInstance().getState();
    SystemSettings settings = DataModel::getInstance().getSettings();
    DataModel::getInstance().unlock();

    const AutomationConfig &cfg = settings.automation;

    if (now - last_eval_ms < cfg.eval_interval_ms) return;
    last_eval_ms = now;

    const StageProfile &profile = kStageProfiles[clamp_stage(state.active_stage)];

    // Always compute health regardless of mode
    SystemHealthLevel health = computeHealth(state, profile);

    if (cfg.mode == AUTOMODE_MANUAL) {
        DataModel::getInstance().lock();
        DataModel::getInstance().getState().health = health;
        DataModel::getInstance().unlock();
        return;
    }

    // Mega en modo AUTO cierra pH/EC/nutriente en firmware; evitar OUT duplicados y resets por UART.
    if (state.auto_mode && !state.maintenance_mode) {
        DataModel::getInstance().lock();
        DataModel::getInstance().getState().health = health;
        DataModel::getInstance().unlock();
        prev_ph_up = prev_ph_down = prev_ec_dose = prev_cooling = prev_humidify =
            prev_fill = false;
        return;
    }

    if (state.maintenance_mode) {
        DataModel::getInstance().lock();
        DataModel::getInstance().getState().health = health;
        DataModel::getInstance().unlock();
        prev_ph_up = prev_ph_down = prev_ec_dose = prev_cooling = prev_humidify =
            prev_fill = false;
        return;
    }

    if (!state.telemetry_live) return;

    evaluatePH(state, profile, cfg);
    evaluateEC(state, profile, cfg);
    evaluateTemperature(state, profile, cfg);
    evaluateHumidity(state, profile, cfg);
    evaluateWaterLevel(state);

    DataModel::getInstance().lock();
    SystemState &ms = DataModel::getInstance().getState();
    ms.health              = health;
    ms.auto_ph_up_active   = act_ph_up;
    ms.auto_ph_down_active = act_ph_down;
    ms.auto_ec_active      = act_ec_dose || act_ec_dilute;
    ms.auto_temp_active    = act_cooling;
    ms.auto_hum_active     = act_humidify;
    ms.auto_fill_active    = act_fill;
    DataModel::getInstance().unlock();

    applyActions();
}

// ---------- pH control with hysteresis ----------

void AutomationEngine::evaluatePH(const SystemState &st, const StageProfile &sp, const AutomationConfig &cfg) {
    if (!st.ph_probe_ok) {
        act_ph_up   = false;
        act_ph_down = false;
        return;
    }
    const uint32_t now  = millis();
    const float    hyst = cfg.ph_hysteresis;

    if (st.ph < sp.ph_min) {
        act_ph_up   = true;
        act_ph_down = false;
    } else if (st.ph > sp.ph_max) {
        act_ph_up   = false;
        act_ph_down = true;
    } else if (st.ph >= sp.ph_min + hyst && st.ph <= sp.ph_max - hyst) {
        act_ph_up   = false;
        act_ph_down = false;
    }

    if ((act_ph_up || act_ph_down) && (now - last_ph_dose_ms < cfg.min_dose_interval_ms)) {
        act_ph_up   = false;
        act_ph_down = false;
        return;
    }
    if (act_ph_up || act_ph_down) last_ph_dose_ms = now;
}

// ---------- EC / TDS control ----------

void AutomationEngine::evaluateEC(const SystemState &st, const StageProfile &sp, const AutomationConfig &cfg) {
    if (!st.tds_probe_ok) {
        act_ec_dose   = false;
        act_ec_dilute = false;
        return;
    }
    const uint32_t now  = millis();
    const int      hyst = cfg.ec_hysteresis;

    if (st.tds < sp.ppm_min) {
        act_ec_dose   = true;
        act_ec_dilute = false;
    } else if (st.tds > sp.ppm_max) {
        act_ec_dose   = false;
        act_ec_dilute = true;
    } else if (st.tds >= sp.ppm_min + hyst && st.tds <= sp.ppm_max - hyst) {
        act_ec_dose   = false;
        act_ec_dilute = false;
    }

    if ((act_ec_dose || act_ec_dilute) && (now - last_ec_dose_ms < cfg.min_dose_interval_ms)) {
        act_ec_dose   = false;
        act_ec_dilute = false;
        return;
    }
    if (act_ec_dose || act_ec_dilute) last_ec_dose_ms = now;
}

// ---------- Temperature control ----------

void AutomationEngine::evaluateTemperature(const SystemState &st, const StageProfile &sp, const AutomationConfig &cfg) {
    if (st.temp_air > sp.ta_max) {
        act_cooling = true;
    } else if (st.temp_air < sp.ta_max - cfg.temp_hysteresis) {
        act_cooling = false;
    }
}

// ---------- Humidity control ----------

void AutomationEngine::evaluateHumidity(const SystemState &st, const StageProfile &sp, const AutomationConfig &cfg) {
    if (st.hum_air < sp.hum_min) {
        act_humidify = true;
    } else if (st.hum_air >= sp.hum_min + cfg.hum_hysteresis) {
        act_humidify = false;
    }
}

// ---------- Water level ----------

void AutomationEngine::evaluateWaterLevel(const SystemState &st) {
    if (st.level_min)       act_fill = true;
    else if (st.level_max)  act_fill = false;
}

// ---------- Health computation ----------

SystemHealthLevel AutomationEngine::computeHealth(const SystemState &st, const StageProfile &sp) {
    if (st.current_alarm > 0) return HEALTH_ALERT;

    int warnings = 0, alerts = 0;

    auto check_range = [&](float val, float lo, float hi, float alert_margin) {
        if (val < lo - alert_margin || val > hi + alert_margin) ++alerts;
        else if (val < lo || val > hi) ++warnings;
    };

    if (st.ph_probe_ok)
        check_range(st.ph, sp.ph_min, sp.ph_max, 0.5f);
    if (st.tds_probe_ok)
        check_range((float)st.tds, (float)sp.ppm_min, (float)sp.ppm_max, 200.0f);
    if (st.tw_probe_ok)
        check_range(st.temp_water, sp.tw_min, sp.tw_max, 3.0f);
    check_range(st.temp_air,   sp.ta_min,  sp.ta_max,  3.0f);
    check_range(st.hum_air,    sp.hum_min, sp.hum_max, 10.0f);

    if (st.level_min) ++warnings;

    if (alerts > 0) return HEALTH_ALERT;
    if (warnings > 0) return HEALTH_WARNING;
    return HEALTH_NORMAL;
}

// ---------- Send UART commands on state change ----------

void AutomationEngine::applyActions() {
    if (act_ph_up != prev_ph_up) {
        sendActuatorCmd("PHU", act_ph_up);
        prev_ph_up = act_ph_up;
    }
    if (act_ph_down != prev_ph_down) {
        sendActuatorCmd("PHD", act_ph_down);
        prev_ph_down = act_ph_down;
    }
    if (act_ec_dose != prev_ec_dose) {
        sendActuatorCmd("PA", act_ec_dose);
        sendActuatorCmd("PB", act_ec_dose);
        prev_ec_dose = act_ec_dose;
    }

    const bool need_pump_in = act_fill || act_ec_dilute;
    if (need_pump_in != prev_fill) {
        sendActuatorCmd("PIN", need_pump_in);
        prev_fill = need_pump_in;
    }

    if (act_cooling != prev_cooling) {
        sendActuatorCmd("INT", act_cooling);
        sendActuatorCmd("EXT", act_cooling);
        prev_cooling = act_cooling;
    }
    if (act_humidify != prev_humidify) {
        sendActuatorCmd("REC", act_humidify);
        prev_humidify = act_humidify;
    }
}
