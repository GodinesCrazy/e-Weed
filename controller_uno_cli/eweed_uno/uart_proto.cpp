#include "uart_proto.h"
#include "actuators.h"
#include "alarms.h"
#include "automation.h"
#include "config_pins.h"
#include "sensors.h"
#include "stage_profiles.h"
#include "storage.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Calibración viva (definida en main.cpp). */
extern CalibrationData g_calibration;

namespace {

unsigned long s_lastStatusMs = 0;
char          s_usbLine[HwConfig::kUsbLineCap];
uint8_t       s_usbLen       = 0;
char          s_hmiLine[HwConfig::kHmiLineCap];
uint8_t       s_hmiLen       = 0;

void sendLineBoth(const char *line) {
  Serial.println(line);
  Serial1.println(line);
}

void appendToken(char *buffer, size_t size, const char *text) {
  strncat(buffer, text, size - strlen(buffer) - 1);
}

void appendKeyValue(char *buffer, size_t size, const char *key, const char *value) {
  if (buffer[0] != '\0') {
    appendToken(buffer, size, ";");
  }
  appendToken(buffer, size, key);
  appendToken(buffer, size, "=");
  appendToken(buffer, size, value);
}

void appendIntField(char *buffer, size_t size, const char *key, long value) {
  char tmp[16];
  ltoa(value, tmp, 10);
  appendKeyValue(buffer, size, key, tmp);
}

void appendFloatField(char *buffer, size_t size, const char *key, float value, uint8_t decimals) {
  char tmp[20];
  dtostrf(value, 0, decimals, tmp);
  char compact[20];
  uint8_t idx = 0;
  for (uint8_t i = 0; tmp[i] != '\0' && idx < sizeof(compact) - 1; ++i) {
    if (tmp[i] != ' ') {
      compact[idx++] = tmp[i];
    }
  }
  compact[idx] = '\0';
  appendKeyValue(buffer, size, key, compact);
}

void sanitizeActField(char *dst, size_t dstLen, const char *src) {
  size_t j = 0;
  for (size_t i = 0; src[i] != '\0' && j + 1 < dstLen; ++i) {
    char c = src[i];
    if (c == ';') {
      c = ',';
    }
    dst[j++] = c;
  }
  dst[j] = '\0';
}

void trimInPlace(char *text) {
  size_t len = strlen(text);
  while (len > 0 && (text[len - 1] == '\r' || text[len - 1] == '\n' || text[len - 1] == ' ' ||
                     text[len - 1] == '\t')) {
    text[--len] = '\0';
  }
  size_t start = 0;
  while (text[start] == ' ' || text[start] == '\t') {
    ++start;
  }
  if (start > 0) {
    memmove(text, text + start, strlen(text + start) + 1);
  }
}

struct OutKeyMap {
  const char *key;
  RelayId     id;
};

static const OutKeyMap kOutMap[] = {
    {"PA", RelayId::kPumpA},
    {"A", RelayId::kPumpA},
    {"PB", RelayId::kPumpB},
    {"B", RelayId::kPumpB},
    {"PHU", RelayId::kPhUp},
    {"PH_UP", RelayId::kPhUp},
    {"PHD", RelayId::kPhDown},
    {"PH_DOWN", RelayId::kPhDown},
    {"REC", RelayId::kRecirculation},
    {"RECIRC", RelayId::kRecirculation},
    {"PIN", RelayId::kWaterIn},
    {"FILL", RelayId::kWaterIn},
    {"INTAKE", RelayId::kWaterIn},
    {"LUZ", RelayId::kLight},
    {"LIGHT", RelayId::kLight},
    {"INT", RelayId::kIntractor},
    {"EXT", RelayId::kExtractor},
    {"EXTRACT", RelayId::kExtractor},
    {"BUZ", RelayId::kBuzzer},
};

int relayFromOutKey(char *nameUpper) {
  for (size_t i = 0; i < sizeof(kOutMap) / sizeof(kOutMap[0]); ++i) {
    if (strcmp(nameUpper, kOutMap[i].key) == 0) {
      return static_cast<int>(kOutMap[i].id);
    }
  }
  return -1;
}

void toUpperAscii(char *s) {
  for (; *s; ++s) {
    if (*s >= 'a' && *s <= 'z') {
      *s = static_cast<char>(*s - 'a' + 'A');
    }
  }
}

void processStream(Stream &stream, char *buffer, uint8_t &length, uint8_t capacity, bool fromHmi) {
  while (stream.available() > 0) {
    const char incoming = static_cast<char>(stream.read());
    if (incoming == '\r') {
      continue;
    }
    if (incoming == '\n') {
      buffer[length] = '\0';
      UartProto_processCommandLine(buffer, fromHmi);
      length       = 0;
      buffer[0]   = '\0';
      continue;
    }
    if (length < capacity - 1) {
      buffer[length++] = incoming;
      buffer[length]     = '\0';
    } else {
      length     = 0;
      buffer[0] = '\0';
      UartProto_sendErr("LINE_TOO_LONG");
    }
  }
}

void handleLocalUsbCommand(char *line) {
  trimInPlace(line);
  if (line[0] == '\0') {
    return;
  }
  toUpperAscii(line);

  if (strcmp(line, "HELP") == 0) {
    Serial.println(F("Comandos: status | help | sim on|off | mode auto|manual|maint | relay <KEY> 0|1 | "
                     "SAFE_RELEASE"));
    return;
  }
  if (strcmp(line, "STATUS") == 0) {
    UartProto_sendStatus();
    return;
  }
  if (strncmp(line, "SIM ", 4) == 0) {
    if (strcmp(line + 4, "ON") == 0) {
      Sensors::setSimulationEnabled(true);
      Serial.println(F("SIM ON"));
    } else if (strcmp(line + 4, "OFF") == 0) {
      Sensors::setSimulationEnabled(false);
      Serial.println(F("SIM OFF"));
    } else {
      Serial.println(F("Uso: sim on | sim off"));
    }
    return;
  }
  if (strncmp(line, "MODE ", 5) == 0) {
    if (strcmp(line + 5, "AUTO") == 0) {
      char err[24];
      if (Automation::tryEnterAuto(err, sizeof(err))) {
        UartProto_sendAck("MODE_AUTO");
      } else {
        if (strcmp(err, "SAFE_STARTUP") == 0) {
          UartProto_sendErr("SAFE_STARTUP");
        } else {
          UartProto_sendErr("NO_RTC");
        }
      }
    } else if (strcmp(line + 5, "MANUAL") == 0) {
      Automation::setOperatingMode(OperatingMode::kManual);
      UartProto_sendAck("MODE_MANUAL");
    } else if (strcmp(line + 5, "MAINT") == 0) {
      Automation::setOperatingMode(OperatingMode::kMaintenance);
      UartProto_sendAck("MODE_MAINT");
    } else {
      Serial.println(F("Uso: mode auto|manual|maint"));
    }
    return;
  }
  if (strcmp(line, "SAFE_RELEASE") == 0) {
    Automation::releaseSafeStartup();
    UartProto_sendAck("SAFE_RELEASE");
    return;
  }
  if (strncmp(line, "RELAY ", 6) == 0) {
    char *p = line + 6;
    char *space = strchr(p, ' ');
    if (space == nullptr) {
      Serial.println(F("Uso: relay <KEY> 0|1"));
      return;
    }
    *space++ = '\0';
    toUpperAscii(p);
    const int st = atoi(space);
    const int ri = relayFromOutKey(p);
    if (ri < 0) {
      Serial.println(F("KEY desconocida"));
      return;
    }
    Actuators::setRelay(static_cast<RelayId>(ri), st != 0, Sensors::data());
    Serial.println(F("OK relay"));
    return;
  }
  Serial.println(F("Comando USB no reconocido: escribe HELP"));
}

}  // namespace

void UartProto_begin() {
  s_lastStatusMs = 0;
  s_usbLen       = 0;
  s_hmiLen       = 0;
  Serial.begin(HwConfig::kUsbBaud);
  Serial1.begin(HwConfig::kHmiBaud);
}

void UartProto_pollUsb() { processStream(Serial, s_usbLine, s_usbLen, sizeof(s_usbLine), false); }

void UartProto_pollHmi() { processStream(Serial1, s_hmiLine, s_hmiLen, sizeof(s_hmiLine), true); }

void UartProto_periodicStatusIfDue() {
  const unsigned long now = millis();
  if (now - s_lastStatusMs >= HwConfig::kStatusIntervalMs) {
    UartProto_sendStatus();
  }
}

void UartProto_sendAck(const char *payload) {
  char msg[64];
  snprintf(msg, sizeof(msg), "ACK:%s", payload);
  sendLineBoth(msg);
}

void UartProto_sendErr(const char *payload) {
  char msg[64];
  snprintf(msg, sizeof(msg), "ERR:%s", payload);
  sendLineBoth(msg);
}

void UartProto_sendAlarm(const char *payload) {
  char msg[72];
  snprintf(msg, sizeof(msg), "ALM:%s", payload);
  sendLineBoth(msg);
}

void UartProto_notifyAlarmRaised(AlarmCode code) {
  if (Alarms::active() && Alarms::current() == code) {
    return;
  }
  Alarms::raise(code);

  switch (code) {
    case AlarmCode::kRtcMissing:
      UartProto_sendErr("AUTO_NEEDS_RTC");
      UartProto_sendAlarm("RTC_FALTANTE");
      break;
    case AlarmCode::kSensorPh:
      UartProto_sendErr("SENSOR_PH");
      UartProto_sendAlarm("PH_INVALIDO");
      break;
    case AlarmCode::kSensorTds:
      UartProto_sendErr("SENSOR_TDS");
      UartProto_sendAlarm("TDS_INVALIDO");
      break;
    case AlarmCode::kSensorDht:
      UartProto_sendErr("SENSOR_DHT");
      UartProto_sendAlarm("DHT_INVALIDO");
      break;
    case AlarmCode::kSensorDs18:
      UartProto_sendErr("SENSOR_DS18");
      UartProto_sendAlarm("TW_INVALIDA");
      break;
    case AlarmCode::kLevelLow:
      UartProto_sendAlarm("NIVEL_BAJO");
      break;
    case AlarmCode::kLevelInconsistent:
      UartProto_sendErr("LEVEL_INCONSISTENT");
      UartProto_sendAlarm("NIVEL_INCONSISTENTE");
      break;
    case AlarmCode::kFillTimeout:
      UartProto_sendAlarm("TIMEOUT_LLENADO");
      break;
    case AlarmCode::kCorrectionLimit:
      UartProto_sendAlarm("MAX_CORRECCIONES");
      break;
    case AlarmCode::kOutputConflict:
      UartProto_sendErr("OUTPUT_CONFLICT");
      UartProto_sendAlarm("INTERLOCK");
      break;
    case AlarmCode::kBadCommand:
      UartProto_sendErr("BAD_COMMAND");
      break;
    case AlarmCode::kStabilizationTimeout:
      UartProto_sendErr("STAB_TIMEOUT");
      UartProto_sendAlarm("STAB_TIMEOUT");
      break;
    case AlarmCode::kModeNotAllowed:
      UartProto_sendErr("MODE_NOT_ALLOWED");
      UartProto_sendAlarm("MODE_NOT_ALLOWED");
      break;
    default:
      break;
  }
}

void UartProto_sendStatus() {
  const SensorData &s = Sensors::data();

  char payload[520];
  payload[0] = '\0';

  appendFloatField(payload, sizeof(payload), "PH", s.phValid ? s.phValue : -1.0f, 2);
  appendIntField(payload, sizeof(payload), "PHRAW", s.phRaw);
  appendIntField(payload, sizeof(payload), "PHOK", s.phValid ? 1 : 0);
  appendFloatField(payload, sizeof(payload), "TDS", s.tdsValid ? s.tdsValue : -1.0f, 0);
  appendIntField(payload, sizeof(payload), "TDSRAW", s.tdsRaw);
  appendIntField(payload, sizeof(payload), "TDSOK", s.tdsValid ? 1 : 0);
  appendFloatField(payload, sizeof(payload), "TW", s.waterTempValid ? s.waterTempC : -1.0f, 1);
  appendIntField(payload, sizeof(payload), "TWOK", s.waterTempValid ? 1 : 0);
  appendFloatField(payload, sizeof(payload), "TA", s.dhtValid ? s.airTempC : -1.0f, 1);
  appendFloatField(payload, sizeof(payload), "HA", s.dhtValid ? s.airHumidity : -1.0f, 1);
  appendIntField(payload, sizeof(payload), "DHT", s.dhtValid ? 1 : 0);
  appendIntField(payload, sizeof(payload), "NMIN", s.levelMinActive ? 1 : 0);
  appendIntField(payload, sizeof(payload), "NMAX", s.levelMaxActive ? 1 : 0);
  appendIntField(payload, sizeof(payload), "PA", Actuators::getRelay(RelayId::kPumpA) ? 1 : 0);
  appendIntField(payload, sizeof(payload), "PB", Actuators::getRelay(RelayId::kPumpB) ? 1 : 0);
  appendIntField(payload, sizeof(payload), "PHU", Actuators::getRelay(RelayId::kPhUp) ? 1 : 0);
  appendIntField(payload, sizeof(payload), "PHD", Actuators::getRelay(RelayId::kPhDown) ? 1 : 0);
  appendIntField(payload, sizeof(payload), "REC", Actuators::getRelay(RelayId::kRecirculation) ? 1 : 0);
  appendIntField(payload, sizeof(payload), "PIN", Actuators::getRelay(RelayId::kWaterIn) ? 1 : 0);
  appendIntField(payload, sizeof(payload), "LUZ", Actuators::getRelay(RelayId::kLight) ? 1 : 0);
  appendIntField(payload, sizeof(payload), "INT", Actuators::getRelay(RelayId::kIntractor) ? 1 : 0);
  appendIntField(payload, sizeof(payload), "EXT", Actuators::getRelay(RelayId::kExtractor) ? 1 : 0);
  appendIntField(payload, sizeof(payload), "ETAPA", Automation::stageIndex());
  appendIntField(payload, sizeof(payload), "ALARM", static_cast<long>(Alarms::current()));
  appendIntField(payload, sizeof(payload), "AUTO", Automation::isAuto() ? 1 : 0);
  appendIntField(payload, sizeof(payload), "MAINT", Automation::isMaintenance() ? 1 : 0);
  appendIntField(payload, sizeof(payload), "RTC", Sensors::rtcAvailable() ? 1 : 0);
  char actSafe[72];
  sanitizeActField(actSafe, sizeof(actSafe), Automation::lastAction());
  appendKeyValue(payload, sizeof(payload), "ACT", actSafe);
  appendIntField(payload, sizeof(payload), "BUZ", Actuators::getRelay(RelayId::kBuzzer) ? 1 : 0);
  appendIntField(payload, sizeof(payload), "MSTATE", static_cast<long>(Automation::fsmState()));
  appendKeyValue(payload, sizeof(payload), "MSTXT", Automation::fsmToShortName(Automation::fsmState()));
  appendIntField(payload, sizeof(payload), "PHC", Automation::phCorrectionCount());
  appendIntField(payload, sizeof(payload), "TDC", Automation::tdsCorrectionCount());

  char clk[24];
  Sensors::formatClockForSts(clk, sizeof(clk));
  if (clk[0] != '\0') {
    appendKeyValue(payload, sizeof(payload), "CLK", clk);
  }

  char line[560];
  snprintf(line, sizeof(line), "STS:%s", payload);
  sendLineBoth(line);
  s_lastStatusMs = millis();
}

void UartProto_processCommandLine(char *line, bool fromHmi) {
  trimInPlace(line);
  if (line[0] == '\0') {
    return;
  }

  if (!fromHmi) {
    char upperCheck[HwConfig::kUsbLineCap];
    strncpy(upperCheck, line, sizeof(upperCheck) - 1);
    upperCheck[sizeof(upperCheck) - 1] = '\0';
    toUpperAscii(upperCheck);
    if (strcmp(upperCheck, "HELP") == 0 || strcmp(upperCheck, "STATUS") == 0 ||
        strncmp(upperCheck, "SIM ", 4) == 0 || strncmp(upperCheck, "MODE ", 5) == 0 ||
        strcmp(upperCheck, "SAFE_RELEASE") == 0 || strncmp(upperCheck, "RELAY ", 6) == 0) {
      handleLocalUsbCommand(line);
      return;
    }
  }

  if (fromHmi && strncmp(line, "CMD:", 4) == 0) {
    memmove(line, line + 4, strlen(line + 4) + 1);
  }

  if (strcmp(line, "GET_STATUS") == 0) {
    UartProto_sendStatus();
    return;
  }
  if (strcmp(line, "ACK_ALARM") == 0) {
    Alarms::clear();
    Automation::onAlarmCleared();
    UartProto_sendAck("ACK_ALARM");
    return;
  }
  if (strcmp(line, "CAL_PH_START") == 0) {
    Automation::setLastAction("Calibracion pH iniciada");
    UartProto_sendAck("CAL_PH_START");
    return;
  }
  if (strcmp(line, "SAFE_RELEASE") == 0) {
    Automation::releaseSafeStartup();
    UartProto_sendAck("SAFE_RELEASE");
    return;
  }

  if (strncmp(line, "SET_STAGE:", 10) == 0) {
    const int requested = atoi(line + 10);
    uint8_t idx = 0;
    if (requested >= 0 && requested <= 4) {
      idx = static_cast<uint8_t>(requested);
    } else if (requested >= 1 && requested <= 5) {
      idx = static_cast<uint8_t>(requested - 1);
    } else {
      UartProto_sendErr("BAD_STAGE");
      return;
    }
    Automation::setStageIndex(idx);
    Automation::resetCorrectionCounters();
    Automation::setLastAction(stageProfile(idx).name);
    UartProto_sendAck("SET_STAGE");
    return;
  }

  if (strncmp(line, "SET_AUTO:", 9) == 0) {
    const int enabled = atoi(line + 9);
    if (enabled != 0) {
      char err[24];
      if (!Automation::tryEnterAuto(err, sizeof(err))) {
        if (strcmp(err, "SAFE_STARTUP") == 0) {
          UartProto_sendErr("SAFE_STARTUP");
        }
        return;
      }
    } else {
      if (!Automation::isMaintenance()) {
        Automation::setOperatingMode(OperatingMode::kManual);
      }
    }
    UartProto_sendAck("SET_AUTO");
    return;
  }

  if (strncmp(line, "SET_MAINT:", 10) == 0) {
    const int enabled = atoi(line + 10);
    if (enabled != 0) {
      Automation::setOperatingMode(OperatingMode::kMaintenance);
    } else {
      Automation::setOperatingMode(OperatingMode::kManual);
    }
    UartProto_sendAck("SET_MAINT");
    return;
  }

  if (strncmp(line, "CAL_PH_SAVE:", 12) == 0) {
    char *ctx = nullptr;
    char *first = strtok_r(line + 12, ":", &ctx);
    char *second = strtok_r(nullptr, ":", &ctx);
    if (first == nullptr || second == nullptr) {
      UartProto_sendErr("BAD_CAL_PH");
      return;
    }
    g_calibration.phOffset = static_cast<float>(atof(first));
    g_calibration.phSlope  = static_cast<float>(atof(second));
    Storage::saveCalibration(g_calibration);
    UartProto_sendAck("CAL_PH_SAVE");
    return;
  }

  if (strncmp(line, "CAL_TDS_SAVE:", 13) == 0) {
    g_calibration.tdsFactor = static_cast<float>(atof(line + 13));
    if (isnan(g_calibration.tdsFactor) || g_calibration.tdsFactor < 0.05f ||
        g_calibration.tdsFactor > 20.0f) {
      UartProto_sendErr("BAD_TDS_FACTOR");
      return;
    }
    Storage::saveCalibration(g_calibration);
    UartProto_sendAck("CAL_TDS_SAVE");
    return;
  }

  if (strncmp(line, "OUT:", 4) == 0) {
    char *ctx = nullptr;
    char *name = strtok_r(line + 4, ":", &ctx);
    char *value = strtok_r(nullptr, ":", &ctx);
    if (name == nullptr || value == nullptr) {
      UartProto_sendErr("BAD_OUT");
      return;
    }
    toUpperAscii(name);
    if (Automation::isAuto() && !Automation::isMaintenance()) {
      UartProto_sendErr("MANUAL_LOCKED");
      return;
    }
    const int relayIndex = relayFromOutKey(name);
    if (relayIndex < 0) {
      UartProto_sendErr("BAD_OUT");
      return;
    }
    Actuators::setRelay(static_cast<RelayId>(relayIndex), atoi(value) != 0, Sensors::data());
    Automation::setLastAction("OUT manual");
    UartProto_sendAck("OUT");
    return;
  }

  Automation::setLastAction("Comando invalido");
  UartProto_sendErr("BAD_COMMAND");
}
