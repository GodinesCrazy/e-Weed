#ifndef EWEED_STORAGE_H
#define EWEED_STORAGE_H

#include "system_types.h"

namespace Storage {

constexpr uint32_t kCalibrationMagic = 0x45574432UL;

void begin();
void loadCalibration(CalibrationData &out);
void saveCalibration(const CalibrationData &in);

uint8_t checksumOf(const CalibrationData &block);

}  // namespace Storage

#endif
