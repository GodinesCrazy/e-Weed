/**
 * @file main.cpp
 * @brief e-Weed controlador principal - Arduino UNO R4 Minima.
 *
 * Orden de inicio: reles apagados, EEPROM/calibracion, sensores/RTC,
 * automatizacion en modo seguro, UART USB y Serial1 con ESP32 HMI.
 */

#include <Arduino.h>
#include "actuators.h"
#include "alarms.h"
#include "automation.h"
#include "config_pins.h"
#include "sensors.h"
#include "storage.h"
#include "uart_proto.h"
#include <stdio.h>

CalibrationData g_calibration;

static unsigned long s_lastHeartbeatMs = 0;

static void heartbeatLog() {
  const unsigned long now = millis();
  if (now - s_lastHeartbeatMs < HwConfig::kHeartbeatLogMs) {
    return;
  }
  s_lastHeartbeatMs = now;
  const SensorData &s = Sensors::data();
  char              line[180];
  snprintf(line, sizeof(line),
           "LOG mode=%u fsm=%s stage=%u ph=%.2f tds=%.0f tw=%.1f sim=%u safe=%u min=%u max=%u",
           static_cast<unsigned>(Automation::mode()), Automation::fsmToShortName(Automation::fsmState()),
           Automation::stageIndex(), static_cast<double>(s.phValue), static_cast<double>(s.tdsValue),
           static_cast<double>(s.waterTempC), Sensors::simulationEnabled() ? 1U : 0U,
           Automation::safeStartupActive() ? 1U : 0U, s.levelMinActive ? 1U : 0U,
           s.levelMaxActive ? 1U : 0U);
  Serial.println(line);
}

void setup() {
  Actuators::begin();

  Storage::begin();
  Storage::loadCalibration(g_calibration);

  Sensors::begin(&g_calibration);
  Sensors::poll(true);

  Actuators::allOutputsOff(Sensors::data());

  Alarms::begin();
  Automation::begin();

  UartProto_begin();

  UartProto_sendAck("BOOT");
  UartProto_sendStatus();
}

void loop() {
  UartProto_pollUsb();
  UartProto_pollHmi();

  Sensors::poll(false);
  Actuators::tick(Sensors::data());
  Automation::tick();
  UartProto_periodicStatusIfDue();
  heartbeatLog();
}
