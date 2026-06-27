#include "sensors.h"
#include "config_pins.h"
#include <DHT.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <RTClib.h>
#include <Wire.h>
#include <math.h>
#include <string.h>

namespace Sensors {

static DHT                 s_dht(Pins::kDht, DHT22);
static OneWire             s_ow(Pins::kOneWireDs18b20);
static DallasTemperature   s_dallas(&s_ow);
static RTC_DS3231          s_rtc;
static CalibrationData    *s_cal = nullptr;
static SensorData          s_data = {};
static bool                 s_rtcOk = false;
static DateTime             s_lastRtc;
static bool                 s_sim = false;

static unsigned long s_lastSensorMs = 0;
static unsigned long s_lastDhtMs    = 0;
static unsigned long s_lastRtcMs    = 0;

static uint8_t s_invPh   = 0;
static uint8_t s_invTds = 0;
static uint8_t s_invDht = 0;
static uint8_t s_invDs18 = 0;
static uint8_t s_bothLevelsCount = 0;

static bool readLevelActive(uint8_t pin) { return digitalRead(pin) == LOW; }

static uint16_t averageAnalog(uint8_t pin) {
  uint32_t total = 0;
  for (uint8_t i = 0; i < HwConfig::kAdcSamples; ++i) {
    total += analogRead(pin);
    delay(2);
  }
  return static_cast<uint16_t>(total / HwConfig::kAdcSamples);
}

static float adcToVoltage(uint16_t raw) {
  return (static_cast<float>(raw) * HwConfig::kAdcVref) / 1023.0f;
}

static float computePhFromRaw(uint16_t raw) {
  const float v = adcToVoltage(raw);
  return (s_cal->phSlope * v) + s_cal->phOffset;
}

static float computeTdsFromRaw(uint16_t raw, float waterTempC) {
  float voltage = adcToVoltage(raw);
  float compensation = 1.0f + 0.02f * ((isnan(waterTempC) ? 25.0f : waterTempC) - 25.0f);
  if (compensation < 0.10f) {
    compensation = 0.10f;
  }
  voltage /= compensation;
  const float cubic =
      (133.42f * voltage * voltage * voltage) - (255.86f * voltage * voltage) + (857.39f * voltage);
  return cubic * 0.5f * s_cal->tdsFactor;
}

static void readDs18() {
  s_dallas.requestTemperatures();
  const float value = s_dallas.getTempCByIndex(0);
  if (value <= -126.0f || value >= 85.0f) {
    s_data.waterTempValid = false;
    s_data.waterTempC     = NAN;
    if (s_invDs18 < 250) {
      ++s_invDs18;
    }
    return;
  }
  s_invDs18               = 0;
  s_data.waterTempValid   = true;
  s_data.waterTempC       = value;
}

static void readDht() {
  const unsigned long now = millis();
  if (now - s_lastDhtMs < HwConfig::kDhtIntervalMs) {
    return;
  }
  s_lastDhtMs = now;
  const float humidity  = s_dht.readHumidity();
  const float temperature = s_dht.readTemperature();
  if (isnan(humidity) || isnan(temperature)) {
    s_data.dhtValid      = false;
    s_data.airHumidity   = NAN;
    s_data.airTempC      = NAN;
    if (s_invDht < 250) {
      ++s_invDht;
    }
    return;
  }
  s_invDht             = 0;
  s_data.dhtValid      = true;
  s_data.airHumidity   = humidity;
  s_data.airTempC      = temperature;
}

static void readAnalogSensors() {
  s_data.phRaw  = averageAnalog(Pins::kPhAnalog);
  s_data.tdsRaw = averageAnalog(Pins::kTdsAnalog);

  s_data.phValue  = computePhFromRaw(s_data.phRaw);
  s_data.tdsValue = computeTdsFromRaw(s_data.tdsRaw, s_data.waterTempC);

  s_data.phValid = (s_data.phValue >= 0.0f && s_data.phValue <= 14.0f && s_data.phRaw > 3 &&
                    s_data.phRaw < 1020);
  s_data.tdsValid =
      (s_data.tdsValue >= 0.0f && s_data.tdsValue <= 2500.0f && s_data.tdsRaw > 3 && s_data.tdsRaw < 1020);

  if (!s_data.phValid) {
    if (s_invPh < 250) {
      ++s_invPh;
    }
  } else {
    s_invPh = 0;
  }
  if (!s_data.tdsValid) {
    if (s_invTds < 250) {
      ++s_invTds;
    }
  } else {
    s_invTds = 0;
  }
}

void begin(CalibrationData *calibration) {
  s_cal = calibration;

#if defined(ARDUINO_ARCH_RENESAS)
  // El cálculo de pH/TDS de este firmware usa escala ADC 0-1023.
  // Forzamos 10 bits para que UNO R4 mantenga la misma base de calibración.
  analogReadResolution(10);
#endif

  pinMode(Pins::kLevelMin, INPUT_PULLUP);
  pinMode(Pins::kLevelMax, INPUT_PULLUP);
  Wire.begin();
  s_rtcOk = s_rtc.begin();
  if (s_rtcOk && s_rtc.lostPower()) {
    s_rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  s_dht.begin();
  s_dallas.begin();
  poll(true);
}

void setSimulationEnabled(bool on) { s_sim = on; }
bool simulationEnabled() { return s_sim; }

void poll(bool force) {
  const unsigned long now = millis();

  if (force || (now - s_lastSensorMs >= HwConfig::kSensorIntervalMs)) {
    s_lastSensorMs = now;
    if (s_sim) {
      s_data.levelMinActive = false;
      s_data.levelMaxActive = false;
      s_data.phRaw          = 512;
      s_data.tdsRaw         = 400;
      s_data.phValue        = 5.8f;
      s_data.tdsValue       = 650.0f;
      s_data.phValid        = true;
      s_data.tdsValid       = true;
      s_data.waterTempC     = 22.0f;
      s_data.waterTempValid = true;
      s_data.airTempC       = 24.0f;
      s_data.airHumidity    = 55.0f;
      s_data.dhtValid       = true;
      s_invPh = s_invTds = s_invDht = s_invDs18 = 0;
      s_bothLevelsCount     = 0;
    } else {
      s_data.levelMinActive = readLevelActive(Pins::kLevelMin);
      s_data.levelMaxActive = readLevelActive(Pins::kLevelMax);
      if (s_data.levelMinActive && s_data.levelMaxActive) {
        if (s_bothLevelsCount < 250) {
          ++s_bothLevelsCount;
        }
      } else {
        s_bothLevelsCount = 0;
      }
      readDs18();
      readDht();
      readAnalogSensors();
    }
  }

  if (force || (now - s_lastRtcMs >= HwConfig::kRtcPollMs)) {
    s_lastRtcMs = now;
    if (s_rtcOk) {
      s_lastRtc = s_rtc.now();
    }
  }
}

const SensorData &data() { return s_data; }
CalibrationData *calibrationPtr() { return s_cal; }

bool rtcAvailable() { return s_rtcOk || s_sim; }

uint8_t rtcHour() {
  if (s_sim) {
    return static_cast<uint8_t>((millis() / 3600000UL) % 24UL);
  }
  return s_rtcOk ? s_lastRtc.hour() : 0;
}

uint8_t rtcMinute() {
  if (s_sim) {
    return static_cast<uint8_t>((millis() / 60000UL) % 60UL);
  }
  return s_rtcOk ? s_lastRtc.minute() : 0;
}

void formatClockForSts(char *buf, size_t bufLen) {
  if (bufLen < 20) {
    if (bufLen > 0) {
      buf[0] = '\0';
    }
    return;
  }
  if (s_sim) {
    const uint32_t s = millis() / 1000UL;
    snprintf(buf, bufLen, "SIM-%02u:%02u:%02u", static_cast<unsigned>((s / 3600UL) % 24UL),
             static_cast<unsigned>((s / 60UL) % 60UL), static_cast<unsigned>(s % 60UL));
    return;
  }
  if (!s_rtcOk) {
    buf[0] = '\0';
    return;
  }
  snprintf(buf, bufLen, "%04u-%02u-%02uT%02u:%02u:%02u", static_cast<unsigned>(s_lastRtc.year()),
           static_cast<unsigned>(s_lastRtc.month()), static_cast<unsigned>(s_lastRtc.day()),
           static_cast<unsigned>(s_lastRtc.hour()), static_cast<unsigned>(s_lastRtc.minute()),
           static_cast<unsigned>(s_lastRtc.second()));
}

uint8_t invalidPhCount() { return s_invPh; }
uint8_t invalidTdsCount() { return s_invTds; }
uint8_t invalidDhtCount() { return s_invDht; }
uint8_t invalidDs18Count() { return s_invDs18; }
uint8_t levelInconsistentCount() { return s_bothLevelsCount; }

}  // namespace Sensors
