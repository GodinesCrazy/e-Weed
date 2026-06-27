#ifndef EWEED_SETTINGS_STORE_H
#define EWEED_SETTINGS_STORE_H

#include "data_model.h"

void settingsLoadInto(SystemSettings &s);
void settingsSaveFrom(const SystemSettings &s);

#endif
