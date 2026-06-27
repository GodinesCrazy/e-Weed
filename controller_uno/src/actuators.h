#ifndef EWEED_ACTUATORS_H
#define EWEED_ACTUATORS_H

#include "system_types.h"

namespace Actuators {

void begin();
void tick(const SensorData &sensors);

bool relayLevelOn();
bool relayLevelOff();

bool setRelay(RelayId id, bool on, const SensorData &sensors);
bool pulseRelay(RelayId id, uint32_t durationMs, const SensorData &sensors);
bool getRelay(RelayId id);

void allOutputsOff(const SensorData &sensors);
void disableCriticalOutputs(const SensorData &sensors);

void applyInterlocks(const SensorData &sensors);

}  // namespace Actuators

#endif
