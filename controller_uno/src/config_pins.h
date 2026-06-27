/**
 * @file config_pins.h
 * @brief Mapa lógico de pines para e-Weed con Arduino UNO R4 Minima como controlador I/O oficial.
 *
 * Arquitectura oficial:
 * - ESP32-2432S028/CYD: HMI, LVGL, WiFi, API y supervisión.
 * - Arduino UNO R4 Minima: sensores, relés, RTC y seguridad de I/O.
 *
 * Notas de cableado UNO R4 Minima:
 * - Serial  = USB / monitor local.
 * - Serial1 = enlace UART con ESP32 HMI. En Arduino UNO R4 usa D0/RX y D1/TX.
 * - I2C del DS3231 usa el bus Wire oficial del UNO R4: SDA/SCL.
 * - A0 y A1 quedan reservados para sensores analógicos pH y TDS.
 * - A2 y A3 se usan como salidas digitales para completar 10 relés.
 */

#ifndef EWEED_CONFIG_PINS_H
#define EWEED_CONFIG_PINS_H

#include <Arduino.h>

namespace Pins {

// --- Sensores analógicos / digitales UNO R4 ---
constexpr uint8_t kPhAnalog       = A0;
constexpr uint8_t kTdsAnalog      = A1;
constexpr uint8_t kDht            = 2;
constexpr uint8_t kOneWireDs18b20 = 3;
constexpr uint8_t kLevelMin       = 4;  // activo en LOW con INPUT_PULLUP
constexpr uint8_t kLevelMax       = 5;  // activo en LOW con INPUT_PULLUP

// --- Relés (salidas digitales UNO R4) ---
constexpr uint8_t kRelayPumpA         = 6;
constexpr uint8_t kRelayPumpB         = 7;
constexpr uint8_t kRelayPhUp          = 8;
constexpr uint8_t kRelayPhDown        = 9;
constexpr uint8_t kRelayRecirculation = 10;
constexpr uint8_t kRelayWaterIn       = 11;
constexpr uint8_t kRelayLight         = 12;
constexpr uint8_t kRelayIntractor     = 13;
constexpr uint8_t kRelayExtractor     = A2;
constexpr uint8_t kRelayBuzzer        = A3;

// --- Comunicación ---
// Serial  = USB (monitor / CLI local)
// Serial1 = HMI ESP32 (UNO R4 D0/RX, D1/TX)

}  // namespace Pins

namespace HwConfig {
/** true = relé ON con nivel HIGH; false = módulo relé activo en LOW (típico). */
constexpr bool kRelayActiveHigh = false;

constexpr unsigned long kUsbBaud = 115200UL;
constexpr unsigned long kHmiBaud = 115200UL;

constexpr unsigned long kStatusIntervalMs     = 1000UL;
constexpr unsigned long kSensorIntervalMs     = 1000UL;
constexpr unsigned long kDhtIntervalMs        = 2500UL;
constexpr unsigned long kRtcPollMs            = 1000UL;
constexpr unsigned long kHeartbeatLogMs       = 5000UL;
constexpr unsigned long kFillTimeoutMs        = 120000UL;
constexpr unsigned long kDoseTimeoutMs        = 15000UL;
constexpr unsigned long kStabilizationMs      = 20000UL;
constexpr unsigned long kRecircAfterDoseMs    = 15000UL;
constexpr uint8_t       kMaxSensorFailures    = 4;
constexpr uint8_t       kAdcSamples           = 12;
constexpr float         kAdcVref              = 5.0f;

constexpr uint8_t kLevelInconsistentReads = 6;

constexpr float kPhOffsetDefault  = 21.34f;
constexpr float kPhSlopeDefault   = -5.70f;
constexpr float kTdsFactorDefault = 1.00f;

constexpr uint8_t kUsbLineCap = 128;
constexpr uint8_t kHmiLineCap = 128;
}  // namespace HwConfig

#endif  // EWEED_CONFIG_PINS_H
