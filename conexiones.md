# Mapa de conexiones - e-Weed (ESP32-2432S028 + Arduino UNO R4 Minima)

Este documento refleja el cableado segun el firmware actual del proyecto.
Fuente principal: `src/main.cpp`, `src/uart_comm.cpp`, `platformio.ini`, `controller_uno/src/main.cpp`.

## 1) Backbone de comunicacion ESP32 <-> UNO

| Lado ESP32 | Lado UNO R4 | Uso |
|---|---|---|
| GPIO17 (TX2) | D0 (RX1 / Serial1 RX) | ESP32 envia comandos al UNO |
| GPIO16 (RX2) | D1 (TX1 / Serial1 TX) | ESP32 recibe telemetria del UNO |
| GND | GND | Referencia comun obligatoria |

Notas:
- Baudrate: `115200`.
- En firmware hay auto-probe de pines invertidos (16/17 <-> 17/16), pero el cableado recomendado es el de la tabla.
- Recomendado: adaptar nivel logico de `UNO TX (5V)` hacia `ESP32 RX (3.3V)` con divisor o level-shifter.

## 2) Sensores conectados al UNO R4

### DHT22 (temperatura/humedad ambiente)

| Pin DHT22 | Pin UNO R4 |
|---|---|
| VCC | 5V |
| GND | GND |
| DATA | D12 |

Notas:
- Si el DHT22 es suelto (no modulo), usar pull-up de 10k entre DATA y VCC.

### PH-4502C (pH)

| Pin PH-4502C | Pin UNO R4 | Estado en firmware |
|---|---|---|
| V+ | 5V | Usado |
| G (GND #1) | GND | Usado |
| G (GND #2) | GND | Usado |
| PO (analog out) | A2 | Usado para lectura pH |
| TO | A3 | Definido como entrada (reserva/monitoreo) |
| DO | D13 | Definido como entrada (reserva/umbral digital) |

Notas:
- La lectura de pH actual usa `PO -> A2`.
- La calibracion de 2 puntos (Ref1/Ref2 + captura P1/P2 + apply) se aplica sobre esa lectura analogica.

### Sensores de nivel

| Sensor | Pin UNO R4 | Logica |
|---|---|---|
| Nivel minimo | A0 | `INPUT_PULLUP`, activo en LOW |
| Nivel maximo | A1 | `INPUT_PULLUP`, activo en LOW |

### RTC DS3231 (I2C)

| Pin DS3231 | Pin UNO R4 |
|---|---|
| VCC | 5V |
| GND | GND |
| SDA | SDA (bus I2C hardware) |
| SCL | SCL (bus I2C hardware) |

## 3) Actuadores por modulo de reles (UNO R4)

Configuracion actual: rele activo en LOW (`kRelayActiveHigh = false`).

| Canal rele | Pin UNO R4 | Etiqueta firmware | Equipo |
|---|---|---|---|
| IN1 | D2 | LUZ | Luz cultivo |
| IN2 | D3 | INT | Ventilador intractor |
| IN3 | D4 | EXT | Ventilador extractor |
| IN4 | D5 | REC | Bomba recirculacion |
| IN5 | D6 | PUMP_IN | Bomba llenado |
| IN6 | D7 | PUMP_A | Dosificacion nutriente A |
| IN7 | D8 | PUMP_B | Dosificacion nutriente B |
| IN8 | D9 | PH_UP | Dosificacion pH+ |
| IN9 | D10 | PH_DOWN | Dosificacion pH- |

Otros IO del UNO:
- `D11`: buzzer alarma.
- `LED_BUILTIN`: heartbeat del controlador.

## 4) ESP32-2432S028 (HMI) - display y touch

Pines definidos en `platformio.ini`:

| Funcion | GPIO ESP32 |
|---|---|
| TFT_MISO | 12 |
| TFT_MOSI | 13 |
| TFT_SCLK | 14 |
| TFT_CS | 15 |
| TFT_DC | 2 |
| TFT_RST | -1 (sin pin dedicado) |
| TFT_BL | 21 |
| TOUCH_CS | 33 |

## 5) Sensores aun no cableados en firmware (pendiente)

Estos valores existen en UI/protocolo, pero no tienen pin fisico dedicado en el firmware UNO actual:
- EC/TDS real (actualmente no hay lectura analogica EC fisica conectada).
- Temperatura de agua real (actualmente no hay sensor de agua dedicado conectado).

Cuando se agreguen, actualizar este archivo con pin exacto y tipo de sensor.

## 6) Resumen rapido solicitado: PH-4502C

Conexion recomendada ahora mismo:
- `V+ -> 5V UNO`
- `G y G -> GND UNO`
- `PO -> A2 UNO` (lectura principal de pH)
- `TO -> A3 UNO` (reserva)
- `DO -> D13 UNO` (reserva)

Con eso, el sistema puede leer pH y ejecutar calibracion por 2 puntos desde la pantalla.
