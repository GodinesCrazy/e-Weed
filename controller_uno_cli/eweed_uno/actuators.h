#ifndef EWEED_ACTUATORS_H
#define EWEED_ACTUATORS_H

#include "system_types.h"

namespace Actuators {

void begin();
bool relayLevelOn();
bool relayLevelOff();

/** Escribe hardware y estado lógico; aplica interlocks con niveles actuales. */
bool setRelay(RelayId id, bool on, const SensorData &sensors);
bool getRelay(RelayId id);
void allOutputsOff(const SensorData &sensors);
void disableCriticalOutputs(const SensorData &sensors);

void applyInterlocks(const SensorData &sensors);

}  // namespace Actuators

#endif
