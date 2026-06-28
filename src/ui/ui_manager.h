#ifndef EWEED_UI_MANAGER_H
#define EWEED_UI_MANAGER_H

#include <stdint.h>

enum ScreenId : uint8_t {
    SCR_SPLASH = 0,
    SCR_HOME,
    SCR_DASHBOARD,
    SCR_STAGES,
    SCR_SENSORS,
    SCR_ACTUATORS,
    SCR_AUTOMATION,
    SCR_CALIBRATION,
    SCR_ALARMS,
    SCR_SETTINGS_HUB,
    SCR_SET_STAGE,
    SCR_SET_SAFETY,
    SCR_SET_RECIRC,
    SCR_SET_STAB,
    SCR_SET_MICRO,
    SCR_SET_PHOTO,
    SCR_SET_VENT,
    SCR_SET_COMM,
    SCR_SET_DISPLAY,
    SCR_SET_SOUND,
    SCR_SET_DOSING,
    SCR_SET_CLIMATE,
    SCR_SET_SYSTEM,
    SCR_MAINTENANCE,
    SCR_SYSTEM_INFO,
    SCR_COUNT
};

class UiManager {
public:
    static UiManager &getInstance() {
        static UiManager instance;
        return instance;
    }

    void init();
    void update();
    void loadScreen(ScreenId id);
    void bumpActivity();

    ScreenId currentScreen = SCR_HOME;

private:
    UiManager() = default;
};

#endif
