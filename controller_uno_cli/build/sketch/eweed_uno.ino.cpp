#line 1 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
#include <Arduino.h>
#include <DHT.h>
#include <Wire.h>

namespace Config {
constexpr unsigned long kUsbDebugBaud = 115200;
constexpr unsigned long kHmiBaud = 115200;
constexpr unsigned long kStatusIntervalMs = 1000;
constexpr unsigned long kSensorIntervalMs = 250;
constexpr unsigned long kRtcPollIntervalMs = 1000;
constexpr unsigned long kHeartbeatLedIntervalMs = 500;
constexpr unsigned long kDhtReadIntervalMs = 2500;
constexpr unsigned long kDhtOfflineWarnMs = 10000;
constexpr uint8_t kDhtReadRetries = 3;
constexpr unsigned long kDhtRetryDelayMs = 25;
constexpr uint8_t kPhSamples = 12;
constexpr float kPhAdcVref = 5.0f;
constexpr float kPhAdcMax = 1023.0f;

constexpr bool kRelayActiveHigh = false;
constexpr bool kLevelInputsUsePullups = true;
constexpr bool kLevelActiveLow = true;
constexpr bool kEnableDemoTelemetry = false;

constexpr uint8_t kHeartbeatLedPin = LED_BUILTIN;
constexpr uint8_t kBuzzerPin = 11;
constexpr uint8_t kDhtPin = 12;
constexpr uint8_t kDhtType = DHT22;
constexpr uint8_t kPhPoPin = A2;
constexpr uint8_t kPhToPin = A3;
constexpr uint8_t kPhDoPin = 13;
constexpr uint8_t kLevelMinPin = A0;
constexpr uint8_t kLevelMaxPin = A1;

constexpr uint8_t kRelayPins[] = {2, 3, 4, 5, 6, 7, 8, 9, 10};
constexpr size_t kRelayCount = sizeof(kRelayPins) / sizeof(kRelayPins[0]);

constexpr unsigned long kAlarmBuzzOnMs = 150;
constexpr unsigned long kAlarmBuzzOffMs = 850;
}  // namespace Config

enum RelayId : uint8_t {
    RELAY_LIGHT = 0,
    RELAY_INTRACTOR,
    RELAY_EXTRACTOR,
    RELAY_RECIRCULATION,
    RELAY_PUMP_IN,
    RELAY_PUMP_A,
    RELAY_PUMP_B,
    RELAY_PH_UP,
    RELAY_PH_DOWN,
};

enum OperatingMode : uint8_t {
    MODE_BOOT = 0,
    MODE_IDLE,
    MODE_AUTO,
    MODE_MANUAL,
    MODE_MAINTENANCE,
    MODE_ALARM,
};

enum AlarmCode : uint8_t {
    ALARM_NONE = 0,
    ALARM_LEVEL_MIN = 1,
    ALARM_LEVEL_MAX = 2,
    ALARM_RTC_UNAVAILABLE = 3,
    ALARM_OUTPUT_CONFLICT = 4,
    ALARM_BAD_COMMAND = 5,
};

struct StageProfile {
    float phTarget;
    uint16_t tdsTarget;
    uint8_t lightOnHour;
    uint8_t lightOffHour;
    unsigned long recircOnMs;
    unsigned long recircOffMs;
};

struct DateTime {
    uint8_t second = 0;
    uint8_t minute = 0;
    uint8_t hour = 0;
    uint8_t day = 1;
    uint8_t month = 1;
    uint16_t year = 2000;
    bool valid = false;
};

struct SensorSnapshot {
    float ph = 0.0f;
    uint16_t tds = 0;
    float tempWater = 0.0f;
    float tempAir = 0.0f;
    float humAir = 0.0f;
    bool levelMin = false;
    bool levelMax = false;
};

struct PhCalibrationData {
    float refPh1 = 4.00f;
    float refPh2 = 7.00f;
    uint16_t rawPoint1 = 0;
    uint16_t rawPoint2 = 0;
    bool hasPoint1 = false;
    bool hasPoint2 = false;
    float offset = 0.0f;
};

struct ControllerState {
    OperatingMode mode = MODE_BOOT;
    AlarmCode alarmCode = ALARM_NONE;
    bool rtcOnline = false;
    bool autoMode = false;
    bool maintenanceMode = false;
    uint8_t activeStage = 0;
    char alarmMessage[64] = "BOOT";
    char lastAction[64] = "Arranque";
};

class Ds3231Rtc {
public:
    void begin() {
        Wire.begin();
    }

    bool read(DateTime &dt) {
        Wire.beginTransmission(kAddress);
        Wire.write(static_cast<uint8_t>(0x00));
        if (Wire.endTransmission() != 0) {
            dt.valid = false;
            return false;
        }

        constexpr uint8_t kBytes = 7;
        if (Wire.requestFrom(kAddress, kBytes) != kBytes) {
            dt.valid = false;
            return false;
        }

        const uint8_t rawSecond = Wire.read();
        const uint8_t rawMinute = Wire.read();
        const uint8_t rawHour = Wire.read();
        Wire.read();  // day of week
        const uint8_t rawDay = Wire.read();
        const uint8_t rawMonth = Wire.read();
        const uint8_t rawYear = Wire.read();

        dt.second = bcdToDec(rawSecond & 0x7F);
        dt.minute = bcdToDec(rawMinute & 0x7F);
        dt.hour = bcdToDec(rawHour & 0x3F);
        dt.day = bcdToDec(rawDay & 0x3F);
        dt.month = bcdToDec(rawMonth & 0x1F);
        dt.year = 2000 + bcdToDec(rawYear);
        dt.valid = isDateTimePlausible(dt);
        return dt.valid;
    }

private:
    static constexpr uint8_t kAddress = 0x68;

    static uint8_t bcdToDec(uint8_t value) {
        return static_cast<uint8_t>(((value >> 4) * 10) + (value & 0x0F));
    }

    static bool isDateTimePlausible(const DateTime &dt) {
        return dt.second < 60 && dt.minute < 60 && dt.hour < 24 &&
               dt.day >= 1 && dt.day <= 31 && dt.month >= 1 && dt.month <= 12 &&
               dt.year >= 2024 && dt.year <= 2099;
    }
};

HardwareSerial &hmiSerial = Serial1;
Ds3231Rtc rtc;
DHT dht(Config::kDhtPin, Config::kDhtType);
DateTime nowRtc;
SensorSnapshot sensors;
ControllerState controller;
PhCalibrationData phCalibration;

bool desiredRelays[Config::kRelayCount] = {false};
bool actualRelays[Config::kRelayCount] = {false};

String hmiInputBuffer;
unsigned long lastStatusAt = 0;
unsigned long lastSensorAt = 0;
unsigned long lastRtcPollAt = 0;
unsigned long lastHeartbeatAt = 0;
unsigned long lastDhtReadAt = 0;
unsigned long lastDhtWarnAt = 0;
unsigned long lastRecircToggleAt = 0;
unsigned long lastBuzzToggleAt = 0;
bool buzzerState = false;
bool heartbeatState = false;
bool dhtOnline = false;
uint16_t latestPhRaw = 0;
float ecOffset = 0.0f;
float tempWaterOffset = 0.0f;
float tempAirOffset = 0.0f;
float humAirOffset = 0.0f;

const StageProfile kStageProfiles[5] = {
    {5.8f, 400, 6, 18, 15UL * 1000UL, 10UL * 60UL * 1000UL},
    {5.9f, 600, 6, 18, 20UL * 1000UL, 8UL * 60UL * 1000UL},
    {6.0f, 800, 6, 18, 30UL * 1000UL, 6UL * 60UL * 1000UL},
    {6.0f, 1000, 7, 19, 30UL * 1000UL, 5UL * 60UL * 1000UL},
    {6.1f, 1200, 8, 20, 45UL * 1000UL, 5UL * 60UL * 1000UL},
};

const char *kRelayNames[Config::kRelayCount] = {
    "LUZ", "INT", "EXT", "REC", "PUMP_IN", "PUMP_A", "PUMP_B", "PH_UP", "PH_DOWN",
};

const char *kRelayStatusKeys[Config::kRelayCount] = {
    "LUZ", "INT", "EXT", "REC", "PIN", "PA", "PB", "PHU", "PHD",
};

#line 219 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void sanitizeText(char *buffer, size_t size);
#line 231 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void setLastAction(const String &message);
#line 236 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void disableSensitiveOutputs();
#line 245 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void setAlarm(AlarmCode code, const String &message);
#line 262 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void clearAlarmIfSafe();
#line 285 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void writeRelayPin(size_t index, bool on);
#line 293 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void applyRelayOutputs();
#line 299 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void allOutputsOff();
#line 306 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
bool levelInputIsActive(uint8_t pin);
#line 311 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void pollRtc();
#line 319 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
uint16_t readPhRawAveraged();
#line 328 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
float phFromRaw(uint16_t raw);
#line 340 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void updatePhTelemetry();
#line 352 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void updateDemoTelemetry();
#line 406 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void updateSensors();
#line 418 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
bool isLightWindowActive(const DateTime &dt, const StageProfile &stage);
#line 431 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void resetRecirculationTimer();
#line 436 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
bool setMode(OperatingMode mode);
#line 474 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
bool setNamedOutput(const String &name, bool enabled);
#line 503 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void sendAck(const String &payload);
#line 510 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void sendErr(const String &payload);
#line 565 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void handleAutoLogic();
#line 586 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void applySafetyInterlocks();
#line 623 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void updateBuzzer();
#line 641 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void updateHeartbeatLed();
#line 652 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void processCommand(const String &command);
#line 835 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void processHmiSerial();
#line 874 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void setupPins();
#line 894 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void setup();
#line 935 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void loop();
#line 219 "C:\\e-Weed\\controller_uno_cli\\eweed_uno\\eweed_uno.ino"
void sanitizeText(char *buffer, size_t size) {
    if (size == 0) {
        return;
    }
    buffer[size - 1] = '\0';
    for (size_t i = 0; i < size && buffer[i] != '\0'; ++i) {
        if (buffer[i] == ';' || buffer[i] == '\r' || buffer[i] == '\n') {
            buffer[i] = ',';
        }
    }
}

void setLastAction(const String &message) {
    message.toCharArray(controller.lastAction, sizeof(controller.lastAction));
    sanitizeText(controller.lastAction, sizeof(controller.lastAction));
}

void disableSensitiveOutputs() {
    desiredRelays[RELAY_RECIRCULATION] = false;
    desiredRelays[RELAY_PUMP_IN] = false;
    desiredRelays[RELAY_PUMP_A] = false;
    desiredRelays[RELAY_PUMP_B] = false;
    desiredRelays[RELAY_PH_UP] = false;
    desiredRelays[RELAY_PH_DOWN] = false;
}

void setAlarm(AlarmCode code, const String &message) {
    controller.alarmCode = code;
    message.toCharArray(controller.alarmMessage, sizeof(controller.alarmMessage));
    sanitizeText(controller.alarmMessage, sizeof(controller.alarmMessage));
    if (code != ALARM_NONE) {
        controller.mode = MODE_ALARM;
        controller.autoMode = false;
        controller.maintenanceMode = false;
        disableSensitiveOutputs();
    }

    hmiSerial.print("ALM:");
    hmiSerial.println(controller.alarmMessage);
    Serial.print("ALM: ");
    Serial.println(controller.alarmMessage);
}

void clearAlarmIfSafe() {
    if (sensors.levelMin) {
        setAlarm(ALARM_LEVEL_MIN, "Nivel minimo activo");
        return;
    }
    if (sensors.levelMax) {
        setAlarm(ALARM_LEVEL_MAX, "Nivel maximo activo");
        return;
    }
    if (!controller.rtcOnline) {
        setAlarm(ALARM_RTC_UNAVAILABLE, "RTC DS3231 no disponible");
        return;
    }

    controller.alarmCode = ALARM_NONE;
    strncpy(controller.alarmMessage, "OK", sizeof(controller.alarmMessage) - 1);
    controller.alarmMessage[sizeof(controller.alarmMessage) - 1] = '\0';
    controller.mode = MODE_IDLE;
    controller.autoMode = false;
    controller.maintenanceMode = false;
    setLastAction("Alarma reconocida");
}

void writeRelayPin(size_t index, bool on) {
    const uint8_t pin = Config::kRelayPins[index];
    const uint8_t activeLevel = Config::kRelayActiveHigh ? HIGH : LOW;
    const uint8_t inactiveLevel = Config::kRelayActiveHigh ? LOW : HIGH;
    digitalWrite(pin, on ? activeLevel : inactiveLevel);
    actualRelays[index] = on;
}

void applyRelayOutputs() {
    for (size_t i = 0; i < Config::kRelayCount; ++i) {
        writeRelayPin(i, desiredRelays[i]);
    }
}

void allOutputsOff() {
    for (size_t i = 0; i < Config::kRelayCount; ++i) {
        desiredRelays[i] = false;
    }
    applyRelayOutputs();
}

bool levelInputIsActive(uint8_t pin) {
    const int raw = digitalRead(pin);
    return Config::kLevelActiveLow ? (raw == LOW) : (raw == HIGH);
}

void pollRtc() {
    DateTime snapshot;
    controller.rtcOnline = rtc.read(snapshot);
    if (controller.rtcOnline) {
        nowRtc = snapshot;
    }
}

uint16_t readPhRawAveraged() {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < Config::kPhSamples; ++i) {
        sum += static_cast<uint32_t>(analogRead(Config::kPhPoPin));
        delayMicroseconds(250);
    }
    return static_cast<uint16_t>(sum / Config::kPhSamples);
}

float phFromRaw(uint16_t raw) {
    if (phCalibration.hasPoint1 && phCalibration.hasPoint2 && phCalibration.rawPoint1 != phCalibration.rawPoint2) {
        const float slope = (phCalibration.refPh2 - phCalibration.refPh1) /
                            static_cast<float>(static_cast<int32_t>(phCalibration.rawPoint2) - static_cast<int32_t>(phCalibration.rawPoint1));
        return phCalibration.refPh1 + (static_cast<float>(static_cast<int32_t>(raw) - static_cast<int32_t>(phCalibration.rawPoint1)) * slope) + phCalibration.offset;
    }

    // Fallback sin calibracion: aproximacion basada en salida tipica ~2.5V en pH7.
    const float voltage = (static_cast<float>(raw) * Config::kPhAdcVref) / Config::kPhAdcMax;
    return 7.0f + ((2.5f - voltage) / 0.18f) + phCalibration.offset;
}

void updatePhTelemetry() {
    latestPhRaw = readPhRawAveraged();
    float ph = phFromRaw(latestPhRaw);
    if (ph < 0.0f) {
        ph = 0.0f;
    }
    if (ph > 14.0f) {
        ph = 14.0f;
    }
    sensors.ph = ph;
}

void updateDemoTelemetry() {
    const StageProfile &stage = kStageProfiles[controller.activeStage];
    const int tick = static_cast<int>((millis() / 4000UL) % 7UL) - 3;

    sensors.ph = stage.phTarget + (tick * 0.03f);
    sensors.tds = static_cast<uint16_t>(stage.tdsTarget + (tick * 12));
    sensors.tempWater = 20.0f + (tick * 0.2f);
    sensors.tempAir = 23.0f + (tick * 0.3f);
    sensors.humAir = 60.0f + (tick * 0.8f);
}

void updateDhtTelemetry(bool force = false) {
    const unsigned long now = millis();
    if (!force && now - lastDhtReadAt < Config::kDhtReadIntervalMs) {
        return;
    }
    lastDhtReadAt = now;

    float humidity = NAN;
    float tempAir = NAN;
    for (uint8_t attempt = 0; attempt < Config::kDhtReadRetries; ++attempt) {
        humidity = dht.readHumidity();
        tempAir = dht.readTemperature();
        if (!isnan(tempAir) || !isnan(humidity)) {
            break;
        }
        if (attempt + 1 < Config::kDhtReadRetries) {
            delay(Config::kDhtRetryDelayMs);
        }
    }

    bool gotAnyValue = false;
    if (!isnan(tempAir)) {
        sensors.tempAir = tempAir;
        gotAnyValue = true;
    }
    if (!isnan(humidity)) {
        sensors.humAir = humidity;
        gotAnyValue = true;
    }

    if (gotAnyValue) {
        dhtOnline = true;
        return;
    }

    dhtOnline = false;
    if (now - lastDhtWarnAt >= Config::kDhtOfflineWarnMs) {
        lastDhtWarnAt = now;
        Serial.print("WARN: DHT22 sin lectura valida en D");
        Serial.println(Config::kDhtPin);
    }
}

void updateSensors() {
    sensors.levelMin = levelInputIsActive(Config::kLevelMinPin);
    sensors.levelMax = levelInputIsActive(Config::kLevelMaxPin);

    if (Config::kEnableDemoTelemetry) {
        updateDemoTelemetry();
    } else {
        updatePhTelemetry();
        updateDhtTelemetry(false);
    }
}

bool isLightWindowActive(const DateTime &dt, const StageProfile &stage) {
    if (!dt.valid) {
        return false;
    }
    if (stage.lightOnHour == stage.lightOffHour) {
        return true;
    }
    if (stage.lightOnHour < stage.lightOffHour) {
        return dt.hour >= stage.lightOnHour && dt.hour < stage.lightOffHour;
    }
    return dt.hour >= stage.lightOnHour || dt.hour < stage.lightOffHour;
}

void resetRecirculationTimer() {
    desiredRelays[RELAY_RECIRCULATION] = false;
    lastRecircToggleAt = millis();
}

bool setMode(OperatingMode mode) {
    if (mode == MODE_AUTO && !controller.rtcOnline) {
        setAlarm(ALARM_RTC_UNAVAILABLE, "RTC requerido para modo AUTO");
        return false;
    }

    controller.mode = mode;
    controller.autoMode = (mode == MODE_AUTO);
    controller.maintenanceMode = (mode == MODE_MAINTENANCE);

    if (mode == MODE_AUTO || mode == MODE_IDLE || mode == MODE_ALARM) {
        allOutputsOff();
        resetRecirculationTimer();
    }

    switch (mode) {
        case MODE_AUTO:
            setLastAction("Modo AUTO");
            break;
        case MODE_MANUAL:
            setLastAction("Modo MANUAL");
            break;
        case MODE_MAINTENANCE:
            setLastAction("Modo MAINTENANCE");
            break;
        case MODE_IDLE:
            setLastAction("Modo IDLE");
            break;
        case MODE_ALARM:
            setLastAction("Modo ALARM");
            break;
        default:
            break;
    }

    return true;
}

bool setNamedOutput(const String &name, bool enabled) {
    String upper = String(name);
    upper.toUpperCase();

    if (upper == "LUZ" || upper == "LIGHT") {
        desiredRelays[RELAY_LIGHT] = enabled;
    } else if (upper == "INT" || upper == "INTRACTOR") {
        desiredRelays[RELAY_INTRACTOR] = enabled;
    } else if (upper == "EXT" || upper == "EXTRACTOR") {
        desiredRelays[RELAY_EXTRACTOR] = enabled;
    } else if (upper == "REC" || upper == "RECIRCULATION") {
        desiredRelays[RELAY_RECIRCULATION] = enabled;
    } else if (upper == "PIN" || upper == "PUMP_IN") {
        desiredRelays[RELAY_PUMP_IN] = enabled;
    } else if (upper == "PA" || upper == "PUMP_A") {
        desiredRelays[RELAY_PUMP_A] = enabled;
    } else if (upper == "PB" || upper == "PUMP_B") {
        desiredRelays[RELAY_PUMP_B] = enabled;
    } else if (upper == "PHU" || upper == "PH_UP") {
        desiredRelays[RELAY_PH_UP] = enabled;
    } else if (upper == "PHD" || upper == "PH_DOWN") {
        desiredRelays[RELAY_PH_DOWN] = enabled;
    } else {
        return false;
    }

    return true;
}

void sendAck(const String &payload) {
    hmiSerial.print("ACK:");
    hmiSerial.println(payload);
    Serial.print("ACK: ");
    Serial.println(payload);
}

void sendErr(const String &payload) {
    hmiSerial.print("ERR:");
    hmiSerial.println(payload);
    Serial.print("ERR: ");
    Serial.println(payload);
}

void sendStatus(bool force = false) {
    const unsigned long now = millis();
    if (!force && now - lastStatusAt < Config::kStatusIntervalMs) {
        return;
    }
    lastStatusAt = now;

    String packet;
    packet.reserve(256);
    const float correctedTempWater = sensors.tempWater + tempWaterOffset;
    const float correctedTempAir = sensors.tempAir + tempAirOffset;
    float correctedHumAir = sensors.humAir + humAirOffset;
    if (correctedHumAir < 0.0f) {
        correctedHumAir = 0.0f;
    }
    if (correctedHumAir > 100.0f) {
        correctedHumAir = 100.0f;
    }

    packet += "STS:";
    packet += "PH=" + String(sensors.ph, 2);
    packet += ";PHRAW=" + String(latestPhRaw);
    packet += ";TDS=" + String(sensors.tds);
    packet += ";TW=" + String(correctedTempWater, 1);
    packet += ";TA=" + String(correctedTempAir, 1);
    packet += ";HA=" + String(correctedHumAir, 1);
    packet += ";NMIN=" + String(sensors.levelMin ? 1 : 0);
    packet += ";NMAX=" + String(sensors.levelMax ? 1 : 0);

    for (size_t i = 0; i < Config::kRelayCount; ++i) {
        packet += ";";
        packet += kRelayStatusKeys[i];
        packet += "=";
        packet += actualRelays[i] ? "1" : "0";
    }

    packet += ";ETAPA=" + String(controller.activeStage);
    packet += ";ALARM=" + String(controller.alarmCode);
    packet += ";AUTO=" + String(controller.autoMode ? 1 : 0);
    packet += ";MAINT=" + String(controller.maintenanceMode ? 1 : 0);
    packet += ";RTC=" + String(controller.rtcOnline ? 1 : 0);
    packet += ";ACT=" + String(controller.lastAction);

    hmiSerial.println(packet);
    Serial.print("STS_USB:");
    Serial.println(packet);
}

void handleAutoLogic() {
    if (controller.mode != MODE_AUTO) {
        return;
    }

    const StageProfile &stage = kStageProfiles[controller.activeStage];

    desiredRelays[RELAY_LIGHT] = isLightWindowActive(nowRtc, stage);

    const unsigned long now = millis();
    if (desiredRelays[RELAY_RECIRCULATION]) {
        if (now - lastRecircToggleAt >= stage.recircOnMs) {
            desiredRelays[RELAY_RECIRCULATION] = false;
            lastRecircToggleAt = now;
        }
    } else if (now - lastRecircToggleAt >= stage.recircOffMs) {
        desiredRelays[RELAY_RECIRCULATION] = true;
        lastRecircToggleAt = now;
    }
}

void applySafetyInterlocks() {
    if (sensors.levelMin) {
        desiredRelays[RELAY_RECIRCULATION] = false;
        desiredRelays[RELAY_PUMP_A] = false;
        desiredRelays[RELAY_PUMP_B] = false;
        desiredRelays[RELAY_PH_UP] = false;
        desiredRelays[RELAY_PH_DOWN] = false;

        if (controller.alarmCode != ALARM_LEVEL_MIN) {
            setAlarm(ALARM_LEVEL_MIN, "Nivel minimo activo");
        }
    } else if (controller.alarmCode == ALARM_LEVEL_MIN && controller.mode == MODE_ALARM) {
        clearAlarmIfSafe();
    }

    if (sensors.levelMax) {
        desiredRelays[RELAY_PUMP_IN] = false;
        if (controller.alarmCode == ALARM_NONE) {
            setAlarm(ALARM_LEVEL_MAX, "Nivel maximo activo");
        }
    } else if (controller.alarmCode == ALARM_LEVEL_MAX && controller.mode == MODE_ALARM) {
        clearAlarmIfSafe();
    }

    if (desiredRelays[RELAY_PUMP_A] && desiredRelays[RELAY_PUMP_B]) {
        desiredRelays[RELAY_PUMP_A] = false;
        desiredRelays[RELAY_PUMP_B] = false;
        setAlarm(ALARM_OUTPUT_CONFLICT, "PUMP_A y PUMP_B simultaneas");
    }

    if (desiredRelays[RELAY_PH_UP] && desiredRelays[RELAY_PH_DOWN]) {
        desiredRelays[RELAY_PH_UP] = false;
        desiredRelays[RELAY_PH_DOWN] = false;
        setAlarm(ALARM_OUTPUT_CONFLICT, "PH_UP y PH_DOWN simultaneos");
    }
}

void updateBuzzer() {
    const bool shouldBuzz = controller.alarmCode != ALARM_NONE;
    const unsigned long now = millis();

    if (!shouldBuzz) {
        buzzerState = false;
        digitalWrite(Config::kBuzzerPin, LOW);
        return;
    }

    const unsigned long interval = buzzerState ? Config::kAlarmBuzzOnMs : Config::kAlarmBuzzOffMs;
    if (now - lastBuzzToggleAt >= interval) {
        buzzerState = !buzzerState;
        lastBuzzToggleAt = now;
        digitalWrite(Config::kBuzzerPin, buzzerState ? HIGH : LOW);
    }
}

void updateHeartbeatLed() {
    const unsigned long now = millis();
    if (now - lastHeartbeatAt < Config::kHeartbeatLedIntervalMs) {
        return;
    }

    lastHeartbeatAt = now;
    heartbeatState = !heartbeatState;
    digitalWrite(Config::kHeartbeatLedPin, heartbeatState ? HIGH : LOW);
}

void processCommand(const String &command) {
    if (command == "REQ_STATUS" || command == "GET_STATUS") {
        sendStatus(true);
        sendAck("REQ_STATUS");
        return;
    }

    if (command == "ACK_ALARM") {
        clearAlarmIfSafe();
        sendAck("ACK_ALARM");
        return;
    }

    if (command == "SAVE_CONFIG") {
        // Placeholder: actualmente se mantiene en RAM.
        sendAck("SAVE_CONFIG");
        setLastAction("Calibracion guardada (RAM)");
        return;
    }

    if (command.startsWith("CAL:")) {
        const String cal = command.substring(4);

        if (cal.startsWith("PH_REF1:")) {
            const float value = cal.substring(strlen("PH_REF1:")).toFloat();
            if (value <= 0.0f || value >= 14.0f) {
                sendErr("PH_REF1_OUT_OF_RANGE");
                return;
            }
            phCalibration.refPh1 = value;
            if (phCalibration.refPh2 <= phCalibration.refPh1) {
                phCalibration.refPh2 = phCalibration.refPh1 + 0.10f;
            }
            sendAck("CAL:PH_REF1");
            return;
        }

        if (cal.startsWith("PH_REF2:")) {
            const float value = cal.substring(strlen("PH_REF2:")).toFloat();
            if (value <= 0.0f || value >= 14.0f) {
                sendErr("PH_REF2_OUT_OF_RANGE");
                return;
            }
            phCalibration.refPh2 = value;
            if (phCalibration.refPh2 <= phCalibration.refPh1) {
                sendErr("PH_REF2_MUST_BE_GT_REF1");
                return;
            }
            sendAck("CAL:PH_REF2");
            return;
        }

        if (cal == "PH_CAPTURE1") {
            updatePhTelemetry();
            phCalibration.rawPoint1 = latestPhRaw;
            phCalibration.hasPoint1 = true;
            setLastAction("Captura pH P1");
            sendAck("CAL:PH_CAPTURE1");
            return;
        }

        if (cal == "PH_CAPTURE2") {
            updatePhTelemetry();
            phCalibration.rawPoint2 = latestPhRaw;
            phCalibration.hasPoint2 = true;
            setLastAction("Captura pH P2");
            sendAck("CAL:PH_CAPTURE2");
            return;
        }

        if (cal.startsWith("PH_OFFSET:")) {
            phCalibration.offset = cal.substring(strlen("PH_OFFSET:")).toFloat();
            sendAck("CAL:PH_OFFSET");
            return;
        }

        if (cal.startsWith("EC_OFFSET:")) {
            ecOffset = cal.substring(strlen("EC_OFFSET:")).toFloat();
            sendAck("CAL:EC_OFFSET");
            return;
        }

        if (cal.startsWith("TW_OFFSET:")) {
            tempWaterOffset = cal.substring(strlen("TW_OFFSET:")).toFloat();
            sendAck("CAL:TW_OFFSET");
            return;
        }

        if (cal.startsWith("TA_OFFSET:")) {
            tempAirOffset = cal.substring(strlen("TA_OFFSET:")).toFloat();
            sendAck("CAL:TA_OFFSET");
            return;
        }

        if (cal.startsWith("HA_OFFSET:")) {
            humAirOffset = cal.substring(strlen("HA_OFFSET:")).toFloat();
            sendAck("CAL:HA_OFFSET");
            return;
        }

        if (cal == "PH_APPLY") {
            if (!phCalibration.hasPoint1 || !phCalibration.hasPoint2) {
                sendErr("PH_CAL_NEEDS_2_POINTS");
                return;
            }
            if (phCalibration.rawPoint1 == phCalibration.rawPoint2) {
                sendErr("PH_CAL_POINTS_EQUAL");
                return;
            }
            setLastAction("Calibracion pH aplicada");
            sendAck("CAL:PH_APPLY");
            return;
        }

        sendErr("UNKNOWN_CAL_COMMAND");
        return;
    }

    if (command.startsWith("SET_STAGE:")) {
        const int requestedStage = command.substring(strlen("SET_STAGE:")).toInt();
        if (requestedStage < 0 || requestedStage > 4) {
            sendErr("INVALID_STAGE");
            return;
        }

        controller.activeStage = static_cast<uint8_t>(requestedStage);
        resetRecirculationTimer();
        setLastAction("Etapa " + String(requestedStage));
        sendAck("SET_STAGE:" + String(requestedStage));
        return;
    }

    if (command.startsWith("SET_MODE:")) {
        const String mode = command.substring(strlen("SET_MODE:"));
        bool changed = false;
        if (mode == "AUTO") {
            changed = setMode(MODE_AUTO);
        } else if (mode == "MANUAL") {
            changed = setMode(MODE_MANUAL);
        } else if (mode == "MAINT" || mode == "MAINTENANCE") {
            changed = setMode(MODE_MAINTENANCE);
        } else if (mode == "IDLE") {
            changed = setMode(MODE_IDLE);
        } else {
            sendErr("INVALID_MODE");
            return;
        }

        if (changed) {
            sendAck("SET_MODE:" + mode);
        }
        return;
    }

    if (command.startsWith("OUT:")) {
        if (!(controller.mode == MODE_MANUAL || controller.mode == MODE_MAINTENANCE)) {
            sendErr("OUT_REQUIRES_MANUAL_MODE");
            return;
        }

        const int firstSep = command.indexOf(':', 4);
        if (firstSep < 0) {
            sendErr("BAD_OUT_FORMAT");
            return;
        }

        const String outputName = command.substring(4, firstSep);
        const String outputValue = command.substring(firstSep + 1);
        const bool enabled = outputValue == "1";

        if (!setNamedOutput(outputName, enabled)) {
            sendErr("UNKNOWN_OUTPUT");
            return;
        }

        setLastAction("OUT " + outputName + "=" + outputValue);
        sendAck("OUT:" + outputName + ":" + outputValue);
        return;
    }

    sendErr("UNKNOWN_COMMAND");
}

void processHmiSerial() {
    while (hmiSerial.available() > 0) {
        const char incoming = static_cast<char>(hmiSerial.read());
        if (incoming == '\r') {
            continue;
        }

        if (incoming == '\n') {
            String line = hmiInputBuffer;
            hmiInputBuffer = "";
            line.trim();

            if (line.length() == 0) {
                continue;
            }

            Serial.print("RX1: ");
            Serial.println(line);

            const int cmdPos = line.indexOf("CMD:");
            if (cmdPos >= 0) {
                // Acepta tramas limpias "CMD:..." y tambien ruido previo
                // (por ejemplo "TX: CMD:..." proveniente de puentes seriales).
                processCommand(line.substring(cmdPos + 4));
            } else {
                // Ignoramos lineas no-protocolo para no inundar el enlace con errores.
                // Ejemplo: heartbeats o trazas de depuracion.
            }
            continue;
        }

        hmiInputBuffer += incoming;
        if (hmiInputBuffer.length() > 180) {
            hmiInputBuffer = "";
            sendErr("CMD_TOO_LONG");
        }
    }
}

void setupPins() {
    pinMode(Config::kHeartbeatLedPin, OUTPUT);
    digitalWrite(Config::kHeartbeatLedPin, LOW);

    pinMode(Config::kBuzzerPin, OUTPUT);
    digitalWrite(Config::kBuzzerPin, LOW);

    pinMode(Config::kLevelMinPin, Config::kLevelInputsUsePullups ? INPUT_PULLUP : INPUT);
    pinMode(Config::kLevelMaxPin, Config::kLevelInputsUsePullups ? INPUT_PULLUP : INPUT);
    pinMode(Config::kDhtPin, INPUT_PULLUP);
    pinMode(Config::kPhPoPin, INPUT);
    pinMode(Config::kPhToPin, INPUT);
    pinMode(Config::kPhDoPin, INPUT);

    for (size_t i = 0; i < Config::kRelayCount; ++i) {
        pinMode(Config::kRelayPins[i], OUTPUT);
        writeRelayPin(i, false);
    }
}

void setup() {
    Serial.begin(Config::kUsbDebugBaud);
    while (!Serial && millis() < 3000UL) {
        delay(10);
    }

    hmiSerial.begin(Config::kHmiBaud);
    dht.begin();
    delay(1200);  // Ventana de estabilizacion inicial del DHT22 tras power-up.

    setupPins();
    rtc.begin();
    pollRtc();
    updateDhtTelemetry(true);
    updateSensors();
    resetRecirculationTimer();

    controller.mode = controller.rtcOnline ? MODE_IDLE : MODE_ALARM;
    controller.autoMode = false;
    controller.maintenanceMode = false;

    if (controller.rtcOnline) {
        strncpy(controller.alarmMessage, "OK", sizeof(controller.alarmMessage) - 1);
        controller.alarmMessage[sizeof(controller.alarmMessage) - 1] = '\0';
        setLastAction("UNO R4 listo");
    } else {
        setAlarm(ALARM_RTC_UNAVAILABLE, "RTC DS3231 no disponible");
    }

    hmiInputBuffer.reserve(192);

    Serial.println("UNO R4 controller online");
    Serial.print("DHT22 en D");
    Serial.print(Config::kDhtPin);
    Serial.print(": ");
    Serial.println(dhtOnline ? "OK" : "SIN_DATOS");
    Serial.print("PH-4502C PO en A");
    Serial.println(static_cast<int>(Config::kPhPoPin - A0));
    sendStatus(true);
}

void loop() {
    const unsigned long now = millis();

    processHmiSerial();

    if (now - lastSensorAt >= Config::kSensorIntervalMs) {
        lastSensorAt = now;
        updateSensors();
    }

    if (now - lastRtcPollAt >= Config::kRtcPollIntervalMs) {
        lastRtcPollAt = now;
        pollRtc();
    }

    handleAutoLogic();
    applySafetyInterlocks();
    applyRelayOutputs();
    updateBuzzer();
    updateHeartbeatLed();
    sendStatus(false);
}



