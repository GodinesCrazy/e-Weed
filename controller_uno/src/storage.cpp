#include "storage.h"
#include <EEPROM.h>
#include <math.h>

namespace Storage {

uint8_t checksumOf(const CalibrationData &block) {
  const uint8_t *ptr = reinterpret_cast<const uint8_t *>(&block);
  uint8_t          sum = 0;
  for (size_t i = 0; i < sizeof(CalibrationData) - 1; ++i) {
    sum ^= ptr[i];
  }
  return sum;
}

void begin() { /* EEPROM Arduino no requiere begin */ }

void loadCalibration(CalibrationData &out) {
  EEPROM.get(0, out);
  if (out.magic != kCalibrationMagic || out.checksum != checksumOf(out) || isnan(out.phOffset) ||
      isnan(out.phSlope) || isnan(out.tdsFactor)) {
    out.magic      = kCalibrationMagic;
    out.phOffset   = 21.34f;
    out.phSlope    = -5.70f;
    out.tdsFactor  = 1.00f;
    out.checksum   = checksumOf(out);
    EEPROM.put(0, out);
  }
}

void saveCalibration(const CalibrationData &in) {
  CalibrationData copy = in;
  copy.magic    = kCalibrationMagic;
  copy.checksum = checksumOf(copy);
  EEPROM.put(0, copy);
}

}  // namespace Storage
