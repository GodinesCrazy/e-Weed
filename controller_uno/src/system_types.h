#ifndef EWEED_SYSTEM_TYPES_H
#define EWEED_SYSTEM_TYPES_H

#include <Arduino.h>

enum class RelayId : uint8_t {
  kPumpA = 0,
  kPumpB,
  kPhUp,
  kPhDown,
  kRecirculation,
  kWaterIn,
  kLight,
  kIntractor,
  kExtractor,
  kBuzzer,
};
constexpr uint8_t kNumRelays = static_cast<uint8_t>(RelayId::kBuzzer) + 1U;

enum class OperatingMode : uint8_t { kManual = 0, kAuto = 1, kMaintenance = 2 };

/** FSM explícita; valores estables para telemetría MSTATE (uint8). */
enum class MachineState : uint8_t {
  kBoot = 0,
  kSafeIdle,
  kManualIdle,
  kAutoIdle,
  kReadSensors,
  kValidateSensors,
  kCheckLevel,
  kFillWater,
  kEvaluatePh,
  kDosePhUp,
  kDosePhDown,
  kRecircAfterPh,
  kEvaluateTds,
  kDoseA,
  kRecircAfterA,
  kDoseB,
  kRecircAfterB,
  kWaitStabilization,
  kRemeasure,
  kControlLight,
  kControlVentilation,
  kSendStatus,
  kAlarm,
  kMaintenance,
};

enum class AlarmCode : uint8_t {
  kNone = 0,
  kRtcMissing,
  kSensorPh,
  kSensorTds,
  kSensorDht,
  kSensorDs18,
  kLevelLow,
  kLevelInconsistent,
  kFillTimeout,
  kCorrectionLimit,
  kOutputConflict,
  kBadCommand,
  kStabilizationTimeout,
  kModeNotAllowed,
};

enum class RecheckReason : uint8_t { kNone = 0, kPh, kTds };

struct StageProfile {
  const char *name;
  float        phTarget;
  float        phTolerance;
  uint16_t     tdsTarget;
  uint16_t     tdsTolerance;
  uint8_t      lightStartHour;
  uint8_t      lightHours;
  uint16_t     recircOnSec;
  uint16_t     recircOffSec;
  uint16_t     nutrientDoseMs;
  uint16_t     phDoseMs;
  uint8_t      maxCorrections;
  float        ventTempOn;
  float        ventTempOff;
  float        ventHumOn;
  float        ventHumOff;
};

struct CalibrationData {
  uint32_t magic;
  float    phOffset;
  float    phSlope;
  float    tdsFactor;
  uint8_t  checksum;
};

struct SensorData {
  uint16_t phRaw;
  uint16_t tdsRaw;
  float    phValue;
  float    tdsValue;
  float    waterTempC;
  float    airTempC;
  float    airHumidity;
  bool     phValid;
  bool     tdsValid;
  bool     waterTempValid;
  bool     dhtValid;
  bool     levelMinActive;
  bool     levelMaxActive;
};

#endif  // EWEED_SYSTEM_TYPES_H
