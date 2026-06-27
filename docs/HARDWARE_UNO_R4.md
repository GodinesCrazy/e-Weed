# e-Weed — Hardware oficial UNO R4 Minima

Este documento define el hardware oficial del controlador I/O.

## Controlador I/O oficial

- Placa: Arduino UNO R4 Minima.
- Firmware: `controller_uno`.
- Función: sensores, relés, RTC, alarmas e interlocks.

## Pines oficiales

| Función | Pin UNO R4 | Tipo |
|---|---:|---|
| pH analógico | A0 | Entrada analógica |
| TDS/EC analógico | A1 | Entrada analógica |
| DHT22 | D2 | Entrada digital |
| DS18B20 OneWire | D3 | Entrada digital |
| Nivel mínimo | D4 | Entrada digital `INPUT_PULLUP` |
| Nivel máximo | D5 | Entrada digital `INPUT_PULLUP` |
| Relé Nutriente A | D6 | Salida digital |
| Relé Nutriente B | D7 | Salida digital |
| Relé pH+ | D8 | Salida digital |
| Relé pH- | D9 | Salida digital |
| Relé recirculación | D10 | Salida digital |
| Relé agua IN | D11 | Salida digital |
| Relé luz | D12 | Salida digital |
| Relé intractor | D13 | Salida digital |
| Relé extractor | A2 como digital | Salida digital |
| Buzzer / alarma | A3 como digital | Salida digital |
| RTC DS3231 | SDA/SCL | I2C |
| UART HMI ESP32 | Serial1 D0/D1 | UART |

## UART con ESP32

Conexión típica:

| UNO R4 | ESP32 |
|---|---|
| TX1 / D1 | RX configurado en HMI |
| RX1 / D0 | TX configurado en HMI |
| GND | GND común |

La HMI usa UART a 115200 baudios.

## Relés

El firmware está preparado para módulos de relé activo en LOW por defecto:

```cpp
constexpr bool kRelayActiveHigh = false;
```

Si el módulo usado es activo en HIGH, cambiar esa constante en `controller_uno/src/config_pins.h`.

## Recomendaciones de montaje

- Separar físicamente relés/cargas 220V de sensores y lógica 5V/3.3V.
- Usar fuente 12V independiente para bombas si corresponde.
- Unir GND solo donde sea necesario para señales de control.
- Identificar cada cable con etiqueta.
- Probar salidas primero sin cargas reales.
- Probar bombas con agua antes de usar soluciones.

## Límites físicos recomendados

- No alimentar bombas desde el pin 5V del UNO R4.
- No conectar cargas 220V directamente al UNO R4.
- No usar protoboard para cargas de potencia.
- Usar borneras, fusibles y gabinete.

## Validación mínima antes de AUTO

1. Todos los relés apagados en arranque.
2. Nivel mínimo y máximo responden correctamente.
3. RTC responde.
4. pH/TDS entregan valores dentro de rango plausible.
5. HMI recibe `STS` estable.
6. Interlocks bloquean salidas críticas.
