#ifndef AUTOMATION_ENGINE_H
#define AUTOMATION_ENGINE_H

#include "data_model.h"

class AutomationEngine {
public:
    static AutomationEngine &getInstance() {
        static AutomationEngine instance;
        return instance;
    }

    void evaluate();

private:
    AutomationEngine() {}

    bool act_ph_up    = false;
    bool act_ph_down  = false;
    bool act_ec_dose  = false;
    bool act_ec_dilute = false;
    bool act_cooling  = false;
    bool act_humidify = false;
    bool act_fill     = false;

    bool prev_ph_up   = false;
    bool prev_ph_down = false;
    bool prev_ec_dose = false;
    bool prev_cooling = false;
    bool prev_humidify = false;
    bool prev_fill    = false;

    uint32_t last_eval_ms     = 0;
    uint32_t last_ph_dose_ms  = 0;
    uint32_t last_ec_dose_ms  = 0;

    void evaluatePH(const SystemState &st, const StageProfile &sp, const AutomationConfig &cfg);
    void evaluateEC(const SystemState &st, const StageProfile &sp, const AutomationConfig &cfg);
    void evaluateTemperature(const SystemState &st, const StageProfile &sp, const AutomationConfig &cfg);
    void evaluateHumidity(const SystemState &st, const StageProfile &sp, const AutomationConfig &cfg);
    void evaluateWaterLevel(const SystemState &st);
    SystemHealthLevel computeHealth(const SystemState &st, const StageProfile &sp);
    void applyActions();
    void sendActuatorCmd(const char *key, bool on);
};

#endif // AUTOMATION_ENGINE_H
