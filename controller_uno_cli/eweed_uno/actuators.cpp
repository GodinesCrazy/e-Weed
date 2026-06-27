#include "actuators.h"
#include "config_pins.h"

namespace Actuators {

static bool     s_relayOn[kNumRelays] = {false};
static uint8_t  s_pins[kNumRelays]    = {
    Pins::kRelayPumpA,
    Pins::kRelayPumpB,
    Pins::kRelayPhUp,
    Pins::kRelayPhDown,
    Pins::kRelayRecirculation,
    Pins::kRelayWaterIn,
    Pins::kRelayLight,
    Pins::kRelayIntractor,
    Pins::kRelayExtractor,
    Pins::kRelayBuzzer,
};

static void writeHw(RelayId id, bool on) {
  const uint8_t pin = s_pins[static_cast<uint8_t>(id)];
  digitalWrite(pin, on ? relayLevelOn() : relayLevelOff());
}

bool relayLevelOn() { return HwConfig::kRelayActiveHigh ? HIGH : LOW; }
bool relayLevelOff() { return HwConfig::kRelayActiveHigh ? LOW : HIGH; }

void begin() {
  for (uint8_t i = 0; i < kNumRelays; ++i) {
    pinMode(s_pins[i], OUTPUT);
    digitalWrite(s_pins[i], relayLevelOff());
    s_relayOn[i] = false;
  }
}

bool getRelay(RelayId id) { return s_relayOn[static_cast<uint8_t>(id)]; }

static void setStateOnly(RelayId id, bool on) {
  s_relayOn[static_cast<uint8_t>(id)] = on;
  writeHw(id, on);
}

void applyInterlocks(const SensorData &sensors) {
  if (getRelay(RelayId::kPumpA) && getRelay(RelayId::kPumpB)) {
    setStateOnly(RelayId::kPumpB, false);
  }
  if (getRelay(RelayId::kPhUp) && getRelay(RelayId::kPhDown)) {
    setStateOnly(RelayId::kPhDown, false);
  }
  if (sensors.levelMinActive) {
    setStateOnly(RelayId::kPumpA, false);
    setStateOnly(RelayId::kPumpB, false);
    setStateOnly(RelayId::kPhUp, false);
    setStateOnly(RelayId::kPhDown, false);
  }
  if (sensors.levelMaxActive) {
    setStateOnly(RelayId::kWaterIn, false);
  }
}

bool setRelay(RelayId id, bool on, const SensorData &sensors) {
  setStateOnly(id, on);
  applyInterlocks(sensors);
  return getRelay(id);
}

void allOutputsOff(const SensorData &sensors) {
  for (uint8_t i = 0; i < kNumRelays; ++i) {
    setStateOnly(static_cast<RelayId>(i), false);
  }
  applyInterlocks(sensors);
}

void disableCriticalOutputs(const SensorData &sensors) {
  setStateOnly(RelayId::kPumpA, false);
  setStateOnly(RelayId::kPumpB, false);
  setStateOnly(RelayId::kPhUp, false);
  setStateOnly(RelayId::kPhDown, false);
  setStateOnly(RelayId::kWaterIn, false);
  setStateOnly(RelayId::kRecirculation, false);
  applyInterlocks(sensors);
}

}  // namespace Actuators
