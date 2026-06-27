#ifndef EWEED_SENSORS_H
#define EWEED_SENSORS_H

#include "system_types.h"

namespace Sensors {

void begin(CalibrationData *calibration);
void poll(bool force);

const SensorData &data();
CalibrationData    *calibrationPtr();

bool     rtcAvailable();
uint8_t  rtcHour();
uint8_t  rtcMinute();
/** Formato compacto para STS:CLK (HMI lo muestra tal cual). */
void     formatClockForSts(char *buf, size_t bufLen);

void setSimulationEnabled(bool on);
bool simulationEnabled();

uint8_t invalidPhCount();
uint8_t invalidTdsCount();
uint8_t invalidDhtCount();
uint8_t invalidDs18Count();
uint8_t levelInconsistentCount();

}  // namespace Sensors

#endif
