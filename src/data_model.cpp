#include "data_model.h"
#include <cstring>

const StageProfile kStageProfiles[5] = {
    {"Germinacion / Plantula", 5.8f, 6.1f, 200,  450, 20.0f, 22.0f, 22.0f, 26.0f, 65.0f, 75.0f, "18/6"},
    {"Vegetativa",             5.8f, 6.2f, 500,  900, 19.0f, 22.0f, 22.0f, 28.0f, 55.0f, 70.0f, "18/6"},
    {"Pre-floracion",          5.8f, 6.2f, 800, 1100, 19.0f, 21.0f, 21.0f, 27.0f, 45.0f, 60.0f, "12/12"},
    {"Floracion",              5.9f, 6.3f, 900, 1300, 18.0f, 21.0f, 20.0f, 26.0f, 40.0f, 50.0f, "12/12"},
    {"Maduracion / Lavado",    5.8f, 6.2f, 150,  400, 18.0f, 20.0f, 19.0f, 25.0f, 40.0f, 50.0f, "12/12"},
};

void DataModel::lock()   { portENTER_CRITICAL(&mbox_mutex); }
void DataModel::unlock() { portEXIT_CRITICAL(&mbox_mutex);  }

SystemState    &DataModel::getState()    { return current_state;    }
SystemSettings &DataModel::getSettings() { return current_settings; }

void DataModel::updateState(const SystemState &s) {
    lock();
    current_state = s;
    unlock();
}

void DataModel::updateSettings(const SystemSettings &s) {
    lock();
    current_settings = s;
    unlock();
}

void DataModel::pushAlarmHistory(const char *msg) {
    if (!msg || !msg[0]) {
        return;
    }
    lock();
    for (int i = SystemState::kAlarmHist - 1; i > 0; --i) {
        memcpy(current_state.alarm_history[i], current_state.alarm_history[i - 1],
               sizeof(current_state.alarm_history[0]));
    }
    strncpy(current_state.alarm_history[0], msg, sizeof(current_state.alarm_history[0]) - 1);
    current_state.alarm_history[0][sizeof(current_state.alarm_history[0]) - 1] = '\0';
    if (current_state.alarm_history_count < SystemState::kAlarmHist) {
        ++current_state.alarm_history_count;
    }
    unlock();
}
