#ifndef DATA_MODEL_H
#define DATA_MODEL_H

#include <Arduino.h>

enum SystemHealthLevel : uint8_t {
    HEALTH_NORMAL  = 0,
    HEALTH_WARNING = 1,
    HEALTH_ALERT   = 2
};

enum AutoMode : uint8_t {
    AUTOMODE_MANUAL = 0,
    AUTOMODE_AUTO   = 1,
    AUTOMODE_HYBRID = 2
};

struct StageProfile {
    const char *name;
    float ph_min;
    float ph_max;
    int   ppm_min;
    int   ppm_max;
    float tw_min;
    float tw_max;
    float ta_min;
    float ta_max;
    float hum_min;
    float hum_max;
    const char *photoperiod;
};

extern const StageProfile kStageProfiles[5];
constexpr int kNumStages = 5;

inline int clamp_stage(int s) { return s < 0 ? 0 : (s > 4 ? 4 : s); }

struct HistoryRecord {
    uint32_t timestamp_ms;
    float    ph;
    int      tds;
    float    temp_water;
    float    temp_air;
    float    hum_air;
    uint8_t  level;   // 0=ok  1=low  2=high
    uint8_t  health;  // SystemHealthLevel
};

struct AutomationConfig {
    float    ph_hysteresis         = 0.15f;
    int      ec_hysteresis         = 50;
    float    temp_hysteresis       = 1.0f;
    float    hum_hysteresis        = 3.0f;
    uint32_t min_dose_interval_ms  = 30000;
    uint32_t dose_duration_ms      = 3000;
    uint32_t eval_interval_ms      = 2000;
    AutoMode mode                  = AUTOMODE_AUTO;
};

struct WiFiConfig {
    char     ssid[33]           = "";
    char     password[65]       = "";
    char     ap_ssid[33]        = "e-Weed-AP";
    char     ap_password[65]    = "eweed1234";
    bool     enabled            = true;
    bool     ap_fallback        = true;
    uint32_t sta_timeout_ms     = 15000;
};

struct SystemState {
    float ph          = 0.0f;
    /** PPM/TDS entero derivado de telemetría del controlador UNO R4 */
    int   tds         = 0;
    float temp_water  = 0.0f;
    /** Temperatura NTC modulo PH-4502C (pin TO), °C */
    float temp_water_probe = 0.0f;
    float temp_air    = 0.0f;
    float hum_air     = 0.0f;

    bool level_min = false;
    bool level_max = false;
    /** Salida digital DO del PH-4502C (1 = HIGH) */
    bool ph_do_high = false;

    bool state_light         = false;
    bool state_intractor     = false;
    bool state_extractor     = false;
    bool state_recirculation = false;
    bool state_pump_in       = false;
    bool state_pump_a        = false;
    bool state_pump_b        = false;
    bool state_ph_up         = false;
    bool state_ph_down       = false;
    /** Buzzer / relé BUZ del controlador UNO R4 */
    bool state_buzzer        = false;
    bool auto_mode           = false;
    bool maintenance_mode    = false;
    bool rtc_online          = false;

    int  active_stage  = 0;
    char last_action[64]   = "Esperando telemetria UART";
    int  current_alarm = 0;
    char alarm_message[64] = "Todo dentro de parametros";
    /** FSM del controlador I/O; 255 = desconocido. Nombre legado preservado por compatibilidad. */
    uint8_t mega_machine_state = 255;
    /** Contadores del controlador I/O. Nombres legados preservados por compatibilidad. */
    uint8_t ph_corrections_mega = 0;
    uint8_t tds_corrections_mega = 0;
    /** Hora RTC del controlador "HH:MM:SS" o vacío */
    char controller_clock[12] = "";

    /** DHT22 en UNO R4; si el STS no trae DHT= se asume true (compat firmware antiguo). */
    bool     dht_online       = true;
    /** PHOK/TDSOK/TWOK en STS (UNO R4); sin clave se asume true (compat). */
    bool     ph_probe_ok      = true;
    bool     tds_probe_ok     = true;
    /** TWOK: DS18 o NTC To validos en UNO R4. */
    bool     tw_probe_ok      = true;

    bool     uart_connected   = false;
    bool     telemetry_live   = false;
    uint32_t uart_last_rx_ms  = 0;
    uint32_t uart_ok_packets  = 0;
    uint32_t uart_bad_packets = 0;

    SystemHealthLevel health = HEALTH_NORMAL;

    bool     wifi_connected = false;
    bool     wifi_ap_mode   = false;
    char     wifi_ip[16]    = "";
    int32_t  wifi_rssi      = 0;

    bool auto_ph_up_active   = false;
    bool auto_ph_down_active = false;
    bool auto_ec_active      = false;
    bool auto_temp_active    = false;
    bool auto_hum_active     = false;
    bool auto_fill_active    = false;

    uint32_t uptime_seconds = 0;
    uint32_t history_count  = 0;

    /** Historial corto de alarmas (más reciente primero) */
    static constexpr int kAlarmHist = 5;
    char     alarm_history[kAlarmHist][40] = {};
    uint8_t  alarm_history_count = 0;
};

struct SystemSettings {
    float ph_target[5]  = {6.0f, 6.0f, 6.0f, 6.0f, 6.0f};
    int   tds_target[5] = {400, 600, 800, 1000, 1200};
    bool  auto_mode        = true;
    bool  maintenance_mode = false;
    bool  dark_theme       = true;
    /** Ajustes locales (NVS); el controlador UNO R4 aplica perfiles/seguridad en AUTO */
    uint16_t safety_ppm_ceiling = 2500;
    float    safety_ph_ceiling  = 8.5f;
    uint32_t recirc_on_sec      = 600;
    uint32_t recirc_off_sec     = 1200;
    uint32_t stabilize_sec      = 180;
    float    microdose_ml       = 5.0f;
    uint8_t  photo_on_hour      = 6;
    uint8_t  photo_hours        = 18;
    float    vent_temp_on_c     = 28.0f;
    float    vent_temp_off_c    = 26.0f;
    uint32_t uart_baud          = 115200;
    int8_t   uart_rx_pin        = 22;
    int8_t   uart_tx_pin        = 27;
    uint8_t  brightness_pct     = 100;
    bool     sound_enabled        = true;
    uint32_t ui_idle_timeout_ms  = 120000;

    float ph_cal_ref1       = 4.00f;
    float ph_cal_ref2       = 7.00f;
    float ph_offset         = 0.0f;
    float ph_slope          = 1.0f;
    float ec_offset         = 0.0f;
    float tds_cal_factor    = 1.0f;
    float temp_water_offset = 0.0f;
    float temp_air_offset   = 0.0f;
    float hum_air_offset    = 0.0f;
    bool  calibration_dirty = false;

    AutomationConfig automation;
    WiFiConfig       wifi;
    uint32_t         log_interval_ms = 300000; // 5 min
};

class DataModel {
public:
    static DataModel &getInstance() {
        static DataModel instance;
        return instance;
    }

    SystemState    &getState();
    SystemSettings &getSettings();

    void updateState(const SystemState &newState);
    void updateSettings(const SystemSettings &newSettings);

    void lock();
    void unlock();

    void pushAlarmHistory(const char *msg);

private:
    DataModel() {}
    SystemState    current_state;
    SystemSettings current_settings;
    portMUX_TYPE   mbox_mutex = portMUX_INITIALIZER_UNLOCKED;
};

#endif // DATA_MODEL_H
