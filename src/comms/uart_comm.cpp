#include "comms/uart_comm.h"
#include "data_model.h"
#include <cmath>

HardwareSerial DataSerial(2);

namespace {
constexpr uint32_t kUartTimeoutMs = 3500;
constexpr uint32_t kPollStatusMs = 1200;
constexpr size_t   kMaxPacketLen = 640;

void fixDecimalSeparator(String &s) { s.replace(',', '.'); }
}  // namespace

void UartComm::begin(long baudrate, int rx_pin, int tx_pin) {
    uart_baudrate = baudrate;
    uart_rx_pin   = rx_pin;
    uart_tx_pin   = tx_pin;
    swapped_probe_done = false;
    using_swapped_pins   = false;
    DataSerial.setRxBufferSize(2048);
    DataSerial.begin(baudrate, SERIAL_8N1, uart_rx_pin, uart_tx_pin);
    inputBuffer_uart2.reserve(512);
    inputBuffer_uart0.reserve(256);
    start_ms              = millis();
    last_valid_packet_ms  = 0;
    last_poll_ms          = 0;
    bad_since_last_valid  = 0;
    line_has_noise_uart2  = false;
    line_has_noise_uart0  = false;
    no_link_reported      = false;
    Serial.printf("UART2 iniciado: %ld bps (RX=%d TX=%d)\n", baudrate, rx_pin, tx_pin);
}

void UartComm::reconfigureUart2Pins(int rx_pin, int tx_pin) {
    DataSerial.end();
    delay(15);
    uart_rx_pin = rx_pin;
    uart_tx_pin = tx_pin;
    DataSerial.begin(uart_baudrate, SERIAL_8N1, uart_rx_pin, uart_tx_pin);

    inputBuffer_uart2    = "";
    line_has_noise_uart2 = false;
    bad_since_last_valid = 0;
    last_valid_packet_ms = 0;
    start_ms             = millis();
    no_link_reported     = false;

    DataModel::getInstance().lock();
    SystemState &state = DataModel::getInstance().getState();
    state.uart_connected  = false;
    state.telemetry_live  = false;
    strncpy(state.last_action, "Reconfigurando UART ESP32", sizeof(state.last_action) - 1);
    state.last_action[sizeof(state.last_action) - 1] = '\0';
    DataModel::getInstance().unlock();

    Serial.printf("UART2 reconfigurado: %ld bps (RX=%d TX=%d)\n", uart_baudrate, uart_rx_pin,
                  uart_tx_pin);
}

void UartComm::maybeProbeSwappedPins(uint32_t now_ms) {
    if (swapped_probe_done || last_valid_packet_ms != 0) {
        return;
    }
    if (now_ms - start_ms < (kUartTimeoutMs * 2UL)) {
        return;
    }

    swapped_probe_done = true;
    using_swapped_pins   = true;
    Serial.printf("UART2 sin enlace: probe automatico con pines invertidos RX=%d TX=%d\n",
                  uart_tx_pin, uart_rx_pin);
    reconfigureUart2Pins(uart_tx_pin, uart_rx_pin);
}

void UartComm::sendCommand(const char *cmd) {
    if (DataSerial) {
        Serial.printf("TX[UART2]: %s\n", cmd);
        DataSerial.println(cmd);
    }
}

void UartComm::process() {
    pollArduinoNonBlocking();
    processStream(DataSerial, inputBuffer_uart2, line_has_noise_uart2, "UART2");
    processStream(Serial, inputBuffer_uart0, line_has_noise_uart0, "UART0");
    maybeProbeSwappedPins(millis());
    updateLinkState();
}

bool UartComm::isPacketTypeSupported(const String &type) const {
    return type == "STS" || type == "ACK" || type == "ERR" || type == "ALM";
}

bool UartComm::isPayloadPrintable(char c) const { return c >= 32 && c <= 126; }

void UartComm::processStream(HardwareSerial &stream, String &buffer, bool &has_noise,
                             const char *source_tag) {
    while (stream.available() > 0) {
        const char inChar = static_cast<char>(stream.read());

        if (inChar == '\n') {
            String line = buffer;
            line.trim();
            buffer        = "";
            const bool had_noise = has_noise;
            has_noise            = false;

            if (line.length() == 0) {
                if (had_noise) {
                    registerBadPacket();
                }
                continue;
            }

            processCompletedLine(line, had_noise, source_tag);
            continue;
        }

        if (inChar == '\r') {
            continue;
        }

        if (!isPayloadPrintable(inChar)) {
            has_noise = true;
            continue;
        }

        if (buffer.length() >= kMaxPacketLen) {
            buffer    = "";
            has_noise = true;
            continue;
        }

        buffer += inChar;
    }
}

void UartComm::processCompletedLine(const String &line, bool had_noise, const char *source_tag) {
    if (had_noise) {
        registerBadPacket();
        return;
    }

    const int colonIndex = line.indexOf(':');
    if (colonIndex <= 0) {
        registerBadPacket();
        return;
    }

    const String type    = line.substring(0, colonIndex);
    const String payload = line.substring(colonIndex + 1);
    if (!isPacketTypeSupported(type)) {
        registerBadPacket();
        return;
    }

    registerValidPacket();
    Serial.printf("RX[%s]: %s\n", source_tag, line.c_str());

    if (type == "STS") {
        parseStatusString(payload);
    } else {
        parseAckError(type, payload);
    }
}

void UartComm::registerValidPacket() {
    last_valid_packet_ms = millis();
    bad_since_last_valid = 0;
    no_link_reported     = false;
    if (using_swapped_pins) {
        using_swapped_pins = false;
        Serial.println("UART2: enlace valido detectado con pinout invertido");
    }

    DataModel::getInstance().lock();
    SystemState &state = DataModel::getInstance().getState();
    state.uart_connected = true;
    state.uart_last_rx_ms  = last_valid_packet_ms;
    state.uart_ok_packets++;
    DataModel::getInstance().unlock();
}

void UartComm::registerBadPacket() {
    bad_since_last_valid++;
    DataModel::getInstance().lock();
    DataModel::getInstance().getState().uart_bad_packets++;
    DataModel::getInstance().unlock();
}

void UartComm::updateLinkState() {
    const uint32_t now = millis();
    if (last_valid_packet_ms == 0) {
        if (!no_link_reported && now - start_ms > kUartTimeoutMs) {
            no_link_reported = true;
            DataModel::getInstance().lock();
            SystemState &state = DataModel::getInstance().getState();
            state.uart_connected = false;
            state.telemetry_live = false;
            strncpy(state.last_action, "Sin enlace UART valido", sizeof(state.last_action) - 1);
            state.last_action[sizeof(state.last_action) - 1] = '\0';
            DataModel::getInstance().unlock();
            Serial.println("UART: sin enlace valido (esperando STS/ACK)");
        }
        return;
    }

    if (now - last_valid_packet_ms <= kUartTimeoutMs) {
        return;
    }

    DataModel::getInstance().lock();
    SystemState &state = DataModel::getInstance().getState();
    if (state.uart_connected) {
        state.uart_connected = false;
        state.telemetry_live = false;
        strncpy(state.last_action, "UART desconectado/timeout", sizeof(state.last_action) - 1);
        state.last_action[sizeof(state.last_action) - 1] = '\0';
        Serial.println("UART timeout: sin paquetes validos");
    }
    DataModel::getInstance().unlock();
}

void UartComm::pollArduinoNonBlocking() {
    const uint32_t now = millis();
    if (now - last_poll_ms < kPollStatusMs) {
        return;
    }
    last_poll_ms = now;
    sendCommand("CMD:GET_STATUS");
}

void UartComm::parseStatusString(const String &payload) {
    DataModel::getInstance().lock();
    SystemState &state = DataModel::getInstance().getState();

    bool dht_key_seen = false;
    bool dht_key_ok   = false;
    bool mstate_seen  = false;

    int startPos = 0;
    while (startPos < payload.length()) {
        int delimPos = payload.indexOf(';', startPos);
        if (delimPos == -1) {
            delimPos = payload.length();
        }

        String pair = payload.substring(startPos, delimPos);
        pair.trim();
        int eqPos = pair.indexOf('=');
        if (eqPos > 0) {
            String key = pair.substring(0, eqPos);
            key.trim();
            key.toUpperCase();
            String valStr = pair.substring(eqPos + 1);
            valStr.trim();

            if (key == "PH") {
                fixDecimalSeparator(valStr);
                state.ph = valStr.toFloat();
            } else if (key == "TDS" || key == "EC" || key == "PPM") {
                fixDecimalSeparator(valStr);
                state.tds = static_cast<int>(lroundf(valStr.toFloat()));
            } else if (key == "TW") {
                fixDecimalSeparator(valStr);
                state.temp_water = valStr.toFloat();
            } else if (key == "TWP") {
                fixDecimalSeparator(valStr);
                state.temp_water_probe = valStr.toFloat();
            } else if (key == "TA" || key == "TMPA" || key == "T_AIR") {
                fixDecimalSeparator(valStr);
                state.temp_air = valStr.toFloat();
            } else if (key == "HA" || key == "HUM" || key == "RH") {
                fixDecimalSeparator(valStr);
                state.hum_air = valStr.toFloat();
            } else if (key == "DHT") {
                dht_key_seen = true;
                dht_key_ok   = (valStr == "1");
            } else if (key == "PHOK") {
                state.ph_probe_ok = (valStr == "1");
            } else if (key == "TDSOK") {
                state.tds_probe_ok = (valStr == "1");
            } else if (key == "TWOK") {
                state.tw_probe_ok = (valStr == "1");
            } else if (key == "NMIN") {
                state.level_min = (valStr == "1");
            } else if (key == "NMAX") {
                state.level_max = (valStr == "1");
            } else if (key == "PHDO") {
                state.ph_do_high = (valStr == "1");
            } else if (key == "LUZ") {
                state.state_light = (valStr == "1");
            } else if (key == "INT") {
                state.state_intractor = (valStr == "1");
            } else if (key == "EXT") {
                state.state_extractor = (valStr == "1");
            } else if (key == "REC") {
                state.state_recirculation = (valStr == "1");
            } else if (key == "PIN") {
                state.state_pump_in = (valStr == "1");
            } else if (key == "PA") {
                state.state_pump_a = (valStr == "1");
            } else if (key == "PB") {
                state.state_pump_b = (valStr == "1");
            } else if (key == "PHU") {
                state.state_ph_up = (valStr == "1");
            } else if (key == "PHD") {
                state.state_ph_down = (valStr == "1");
            } else if (key == "BUZ") {
                state.state_buzzer = (valStr == "1");
            } else if (key == "AUTO") {
                state.auto_mode = (valStr == "1");
            } else if (key == "MAINT") {
                state.maintenance_mode = (valStr == "1");
            } else if (key == "RTC") {
                state.rtc_online = (valStr == "1");
            } else if (key == "ETAPA") {
                state.active_stage = valStr.toInt();
            } else if (key == "ALARM") {
                state.current_alarm = valStr.toInt();
            } else if (key == "MSTATE") {
                mstate_seen              = true;
                int v                    = valStr.toInt();
                state.mega_machine_state = (v >= 0 && v <= 255) ? static_cast<uint8_t>(v) : 255;
            } else if (key == "PHC") {
                int v = valStr.toInt();
                state.ph_corrections_mega =
                    (v >= 0 && v <= 255) ? static_cast<uint8_t>(v) : 0;
            } else if (key == "TDC") {
                int v = valStr.toInt();
                state.tds_corrections_mega =
                    (v >= 0 && v <= 255) ? static_cast<uint8_t>(v) : 0;
            } else if (key == "CLK") {
                strncpy(state.controller_clock, valStr.c_str(), sizeof(state.controller_clock) - 1);
                state.controller_clock[sizeof(state.controller_clock) - 1] = '\0';
            } else if (key == "ACT") {
                strncpy(state.last_action, valStr.c_str(), sizeof(state.last_action) - 1);
                state.last_action[sizeof(state.last_action) - 1] = '\0';
            }
        }
        startPos = delimPos + 1;
    }

    if (dht_key_seen) {
        state.dht_online = dht_key_ok;
    }
    if (!mstate_seen) {
        state.mega_machine_state = 255;
    }

    state.telemetry_live  = true;
    state.uart_connected  = true;
    state.uart_last_rx_ms = millis();

    DataModel::getInstance().unlock();
}

void UartComm::parseAckError(const String &type, const String &payload) {
    char hist_line[48] = "";

    DataModel::getInstance().lock();
    SystemState &state = DataModel::getInstance().getState();

    if (type == "ALM") {
        if (state.current_alarm == 0) {
            state.current_alarm = 1;
        }
        strncpy(state.alarm_message, payload.c_str(), sizeof(state.alarm_message) - 1);
        state.alarm_message[sizeof(state.alarm_message) - 1] = '\0';
        snprintf(hist_line, sizeof(hist_line), "ALM %s", payload.c_str());
    } else if (type == "ERR") {
        snprintf(state.last_action, sizeof(state.last_action), "ERROR: %s", payload.c_str());
    } else if (type == "ACK") {
        snprintf(state.last_action, sizeof(state.last_action), "OK: %s", payload.c_str());
    }

    state.uart_connected = true;
    state.uart_last_rx_ms = millis();

    DataModel::getInstance().unlock();

    if (hist_line[0] != '\0') {
        DataModel::getInstance().pushAlarmHistory(hist_line);
    }
}
