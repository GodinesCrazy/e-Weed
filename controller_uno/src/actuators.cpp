#include "actuators.h"
#include "config_pins.h"

namespace Actuators {

static bool     s_relayOn[kNumRelays]    = {false};
static uint32_t s_autoOffAt[kNumRelays]  = {0};
static uint8_t  s_pins[kNumRelays]       = {
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

static bool isCriticalFluidRelay(RelayId id) {
  return id == RelayId::kPumpA || id == RelayId::kPumpB || id == RelayId::kPhUp ||
         id == RelayId::kPhDown || id == RelayId::kRecirculation;
}

bool relayLevelOn() { return HwConfig::kRelayActiveHigh ? HIGH : LOW; }
bool relayLevelOff() { return HwConfig::kRelayActiveHigh ? LOW : HIGH; }

void begin() {
  for (uint8_t i = 0; i < kNumRelays; ++i) {
    pinMode(s_pins[i], OUTPUT);
    digitalWrite(s_pins[i], relayLevelOff());
    s_relayOn[i]   = false;
    s_autoOffAt[i] = 0;
  }
}

bool getRelay(RelayId id) { return s_relayOn[static_cast<uint8_t>(id)]; }

static void setStateOnly(RelayId id, bool on) {
  const uint8_t idx = static_cast<uint8_t>(id);
  s_relayOn[idx] = on;
  if (!on) {
    s_autoOffAt[idx] = 0;
  }
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
    setStateOnly(RelayId::kRecirculation, false);
  }
  if (sensors.levelMaxActive) {
    setStateOnly(RelayId::kWaterIn, false);
  }
}

bool setRelay(RelayId id, bool on, const SensorData &sensors) {
  const uint8_t idx = static_cast<uint8_t>(id);
  s_autoOffAt[idx] = 0;

  if (on) {
    if (sensors.levelMinActive && isCriticalFluidRelay(id)) {
      applyInterlocks(sensors);
      return false;
    }
    if (sensors.levelMaxActive && id == RelayId::kWaterIn) {
      applyInterlocks(sensors);
      return false;
    }
    if (id == RelayId::kPumpA) {
      setStateOnly(RelayId::kPumpB, false);
    } else if (id == RelayId::kPumpB) {
      setStateOnly(RelayId::kPumpA, false);
    } else if (id == RelayId::kPhUp) {
      setStateOnly(RelayId::kPhDown, false);
    } else if (id == RelayId::kPhDown) {
      setStateOnly(RelayId::kPhUp, false);
    }
  }

  setStateOnly(id, on);
  applyInterlocks(sensors);
  return getRelay(id);
}

bool pulseRelay(RelayId id, uint32_t durationMs, const SensorData &sensors) {
  if (durationMs < 100UL) {
    durationMs = 100UL;
  }
  if (durationMs > 300000UL) {
    durationMs = 300000UL;
  }

  if (!setRelay(id, true, sensors)) {
    return false;
  }

  s_autoOffAt[static_cast<uint8_t>(id)] = millis() + durationMs;
  return true;
}

void tick(const SensorData &sensors) {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < kNumRelays; ++i) {
    if (s_autoOffAt[i] != 0 && static_cast<int32_t>(now - s_autoOffAt[i]) >= 0) {
      setStateOnly(static_cast<RelayId>(i), false);
    }
  }
  applyInterlocks(sensors);
}

void allOutputsOff(const SensorData &sensors) {
  for (uint8_t i = 0; i < kNumRelays; ++i) {
    s_autoOffAt[i] = 0;
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
