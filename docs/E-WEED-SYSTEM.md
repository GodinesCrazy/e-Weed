# e-Weed — Sistema de control hidropónico

## Arquitectura

- **ATmega2560 (Mega PRO / CH340)**: control en tiempo real — sensores, relés (o drivers) de bombas peristálticas 12 V, RTC DS3231, lógica de **modo AUTO** (pH, EC/TDS con nutriente A→B secuencial 1:1, fotoperiodo, recirculación, dilución con bomba de entrada si PPM alto), **dosificación por volumen** (`DOSE_*`), calibración pH por UART.
- **ESP32 + TFT + LVGL**: HMI táctil, WiFi/API, registro; UART2 hacia el Mega (`CMD:` / `STS:` / `ACK:` / `ERR:`).
- **Regla de negocio**: en modo **AUTO** en el Mega, el **ESP no envía** `CMD:OUT:*` (evita conflictos y reduce carga UART/reinicios). En **MANUAL** / **MANT** el HMI puede conmutar salidas.

## Conexiones (resumen)

### Mega 2560

| Función | Pin |
|--------|-----|
| Relés / drivers (ej. MOSFET/IRF520) bomba in, recirc, **Nutr.A**, **Nutr.B**, **pH+**, **pH−**, luz, intr., extr. | D2–D10 (ver `Config::kRelayPins` en `controller_uno/src/main.cpp`) |
| DHT22 | D12 |
| Buzzer | D11 |
| PH-4502C PO / TO / DO | A2, A3, D13 |
| TDS analógico | A4 |
| DS18B20 | D22 (Mega; UNO usa A0) |
| I2C RTC | D20 SDA, D21 SCL |
| UART ↔ ESP32 | **Serial1**: TX1=D18 → RX ESP; RX1=D19 ← TX ESP; **GND común** |
| USB | Depuración `Serial` 115200 |

**Bombas peristálticas 12 V (INTLLAB RS385 ~3 W):** no van al Mega en directo. Cada bomba: **12 V**, masa común, **control por driver** (MOSFET de canal N + **diodo flyback** en motor), señal desde el pin de relé/MOSFET. Nutriente A = `PUMP_A`, B = `PUMP_B`, pH+ = `PH_UP`, pH− = `PH_DOWN`. Proporción 1:1 A:B en automático y en `DOSE_AB`.

**EMI / reinicios ESP:** separar alimentación de 12 V de la lógica 5 V/3,3 V; condensadores en alimentación del ESP; cableado corto al UART; considerar optoacopladores en entradas ruidosas.

### ESP32 (CYD / ILI9341)

Ver `platformio.ini`: UART2 típico **RX=16, TX=17** (ajustar si tu cableado difiere). TFT/touch según `TFT_eSPI` y pines del build flags.

## Rangos por etapa (HMI y Mega AUTO)

Definidos en `src/data_model.cpp` (`kStageProfiles`), alineados con `controller_uno` para automatismo local:

| Etapa | pH min–max | PPM min–max | Notas |
|-------|------------|-------------|--------|
| 0 Germinación | 5.8 – 6.1 | 200 – 450 | 18/6 |
| 1 Vegetativa | 5.8 – 6.2 | 500 – 900 | 18/6 |
| 2 Pre-floración | 5.8 – 6.2 | 800 – 1100 | 12/12 |
| 3 Floración | 5.9 – 6.3 | 900 – 1300 | 12/12 |
| 4 Maduración / lavado | 5.8 – 6.2 | 150 – 400 | 12/12 |

Fotoperiodo y tiempos de recirculación siguen la tabla en firmware del Mega (`StageProfile`).

## Comandos UART útiles (Mega)

- `CMD:SET_MODE:AUTO` — requiere **RTC online**; si no: `ERR:AUTO_NEEDS_RTC`.
- `CMD:SET_MODE:MANUAL` / `CMD:SET_MODE:MAINT`
- `CMD:OUT:PA:1` — solo en MANUAL/MANT (claves `PA`,`PB`,`PHU`,`PHD`,`PIN`,`REC`,`LUZ`,`INT`,`EXT`)
- `CMD:DOSE_AB:<ml>` — **ml por cada línea** (A luego B, mismo tiempo calibrado)
- `CMD:DOSE_PHU:<ml>` / `CMD:DOSE_PHD:<ml>`
- Calibración pH: `CMD:CAL:PH_CAPTURE1`, `PH_CAPTURE2`, `PH_APPLY`, offsets (ver firmware)

Caudal por defecto: `Config::kDoseNutrientMlPerSec`, `Config::kDosePhMlPerSec` — **medir con jeringa** y ajustar constantes en `main.cpp`.

## Calibración pH (profesional)

1. Soluciones patrón (p. ej. 4.01 y 7.00) a temperatura estable.
2. Desde HMI pantalla Calibración o UART: fijar referencias, capturar punto 1 y 2, aplicar.
3. Verificar lectura intermedia (6.86) antes de cultivar.

## Mejoras pendientes sugeridas

- Persistir calibración y caudales en EEPROM/Flash.
- Botones en LVGL para `DOSE_AB` / `DOSE_PHU` / `DOSE_PHD` sin cable serial.
- API HTTP `/api/dose` autenticada.
- Debounce táctil adicional si persisten resets bajo carga de red + LVGL.
