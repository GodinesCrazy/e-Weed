#ifndef EWEED_UART_COMM_H
#define EWEED_UART_COMM_H

#include <Arduino.h>

class UartComm {
public:
    static UartComm &getInstance() {
        static UartComm instance;
        return instance;
    }

    void begin(long baudrate, int rx_pin = 22, int tx_pin = 27);
    void process();
    void sendCommand(const char *cmd);

private:
    bool isPacketTypeSupported(const String &type) const;
    bool isPayloadPrintable(char c) const;
    void reconfigureUart2Pins(int rx_pin, int tx_pin);
    void maybeProbeSwappedPins(uint32_t now_ms);
    void processStream(HardwareSerial &stream, String &buffer, bool &has_noise,
                       const char *source_tag);
    void processCompletedLine(const String &line, bool had_noise, const char *source_tag);
    void registerValidPacket();
    void registerBadPacket();
    void updateLinkState();
    void pollArduinoNonBlocking();
    void parseStatusString(const String &payload);
    void parseAckError(const String &type, const String &payload);

    UartComm() {}

    String   inputBuffer_uart2;
    String   inputBuffer_uart0;
    uint32_t last_valid_packet_ms = 0;
    uint32_t last_poll_ms         = 0;
    uint32_t bad_since_last_valid = 0;
    bool     line_has_noise_uart2 = false;
    bool     line_has_noise_uart0 = false;
    uint32_t start_ms             = 0;
    bool     no_link_reported     = false;
    bool     swapped_probe_done   = false;
    bool     using_swapped_pins   = false;
    int      uart_rx_pin          = 22;
    int      uart_tx_pin          = 27;
    long     uart_baudrate        = 115200;
};

#endif
