/**
 * @file config_pins.h
 * @brief Mapa lógico de pines ATmega2560 para e-Weed (Mega 2560 Pro Mini genérico).
 *
 * IMPORTANTE — Mega 2560 Pro Mini:
 * Los números Dxx/Axx son los del núcleo ATmega2560 estándar (Arduino Mega).
 * En la PCB mini la serigrafía puede decir "D22", "SCL", "SDA", etc. en posiciones
 * físicas distintas: cablear según la etiqueta de TU placa y cruzar con esta tabla.
 *
 * I2C del DS3231 en Mega clásico: SDA = D20, SCL = D21 (no D2/D3).
 * UART con ESP32: Serial1 RX1=D19, TX1=D18 (hardware USART1).
 */

#ifndef EWEED_CONFIG_PINS_H
#define EWEED_CONFIG_PINS_H

#include <Arduino.h>

namespace Pins {

// --- Sensores analógicos / digitales (lógico Mega) ---
constexpr uint8_t kPhAnalog      = A0;
constexpr uint8_t kTdsAnalog     = A1;
constexpr uint8_t kDht           = 2;
constexpr uint8_t kOneWireDs18b20 = 3;
constexpr uint8_t kLevelMin      = 4;  // activo en LOW con INPUT_PULLUP
constexpr uint8_t kLevelMax      = 5;

// --- Relés (salidas digitales) ---
constexpr uint8_t kRelayPumpA         = 22;
constexpr uint8_t kRelayPumpB         = 23;
constexpr uint8_t kRelayPhUp          = 24;
constexpr uint8_t kRelayPhDown        = 25;
constexpr uint8_t kRelayRecirculation = 26;
constexpr uint8_t kRelayWaterIn       = 27;
constexpr uint8_t kRelayLight         = 28;
constexpr uint8_t kRelayIntractor     = 29;
constexpr uint8_t kRelayExtractor     = 30;
constexpr uint8_t kRelayBuzzer        = 31;

// --- Comunicación ---
// Serial  = USB (monitor / CLI local)
// Serial1 = HMI ESP32 (mismo baud que la HMI)

}  // namespace Pins

namespace HwConfig {
/** true = relé ON con nivel HIGH; false = módulo relé activo en LOW (típico). */
constexpr bool kRelayActiveHigh = false;

constexpr unsigned long kUsbBaud = 115200UL;
constexpr unsigned long kHmiBaud = 115200UL;

constexpr unsigned long kStatusIntervalMs   = 1000UL;
constexpr unsigned long kSensorIntervalMs   = 1000UL;
constexpr unsigned long kDhtIntervalMs      = 2500UL;
constexpr unsigned long kRtcPollMs          = 1000UL;
constexpr unsigned long kHeartbeatLogMs       = 5000UL;
constexpr unsigned long kFillTimeoutMs        = 120000UL;
constexpr unsigned long kDoseTimeoutMs        = 15000UL;
constexpr unsigned long kStabilizationMs    = 20000UL;
constexpr unsigned long kRecircAfterDoseMs  = 15000UL;
constexpr uint8_t       kMaxSensorFailures  = 4;
constexpr uint8_t       kAdcSamples         = 12;
constexpr float         kAdcVref            = 5.0f;

constexpr uint8_t kLevelInconsistentReads = 6;

constexpr float kPhOffsetDefault  = 21.34f;
constexpr float kPhSlopeDefault   = -5.70f;
constexpr float kTdsFactorDefault = 1.00f;

constexpr uint8_t kUsbLineCap = 128;
constexpr uint8_t kHmiLineCap = 128;
}  // namespace HwConfig

#endif  // EWEED_CONFIG_PINS_H
