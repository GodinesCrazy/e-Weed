#include "alarms.h"
#include "actuators.h"
#include "sensors.h"

namespace Alarms {

static AlarmCode s_code = AlarmCode::kNone;

void begin() { s_code = AlarmCode::kNone; }

bool active() { return s_code != AlarmCode::kNone; }
AlarmCode current() { return s_code; }

void raise(AlarmCode code) {
  if (s_code == code) {
    return;
  }
  s_code = code;
  Actuators::disableCriticalOutputs(Sensors::data());
  Actuators::setRelay(RelayId::kBuzzer, true, Sensors::data());
}

void clear() {
  s_code = AlarmCode::kNone;
  Actuators::setRelay(RelayId::kBuzzer, false, Sensors::data());
}

}  // namespace Alarms
