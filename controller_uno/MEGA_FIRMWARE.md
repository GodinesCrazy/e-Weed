# Firmware Mega 2560 — e-Weed (controlador principal)

## Compatibilidad HMI ESP32-2432S028

La HMI (`src/comms/uart_comm.cpp`) espera líneas `STS:...`, `ACK:...`, `ERR:...`, `ALM:...` con claves como `PH`, `TDS`, `TW`, `TA`, `HA`, `NMIN`, `NMAX`, `LUZ`, `INT`, `EXT`, `REC`, `PA`, `PB`, `PHU`, `PHD`, `PIN`, `AUTO`, `MAINT`, `RTC`, `ETAPA`, `ALARM`, `ACT`, `CLK`, `BUZ`, `MSTATE`, `PHC`, `TDC`. Este firmware las emite. Campo extra **`MSTXT`**: nombre corto ASCII de la FSM (la HMI puede ignorarlo).

Comandos aceptados (con prefijo `CMD:` desde Serial1 / HMI): `GET_STATUS`, `SET_STAGE:X`, `SET_AUTO:0|1`, `SET_MAINT:0|1`, `CAL_PH_START`, `CAL_PH_SAVE:offset:slope`, `CAL_TDS_SAVE:factor`, `ACK_ALARM`, `OUT:KEY:0|1`, `SAFE_RELEASE`.

Alias de `OUT:` (además de `PA`…`BUZ` usados por la HMI): `A`, `B`, `PH_UP`, `PH_DOWN`, `RECIRC`, `FILL`, `INTAKE`, `LIGHT`, `EXTRACT`.

## Estructura de archivos (`controller_uno/src/`)

| Archivo | Rol |
|---------|-----|
| `main.cpp` | `setup` / `loop`, calibración global, heartbeat USB |
| `config_pins.h` | Pines lógicos Mega + notas Pro Mini |
| `system_types.h` | `RelayId`, modos, FSM, alarmas, structs |
| `stage_profiles.h` | Perfiles de las 5 etapas |
| `sensors.cpp/h` | RTC (RTClib DS3231), DHT22, DS18B20, pH/TDS analógicos, niveles, simulación |
| `actuators.cpp/h` | Relés, interlocks A/B, pH+/pH-, nivel mín/máx |
| `storage.cpp/h` | EEPROM calibración pH/TDS |
| `alarms.cpp/h` | Código de alarma + buzzer |
| `uart_proto.cpp/h` | STS/ACK/ERR/ALM, parser de comandos, CLI USB |
| `automation.cpp/h` | FSM AUTO, recirculación programada, luz/vent |

## Arranque seguro (placa nueva)

1. `MAINT=1`, `AUTO=0`, `SAFE_STARTUP` latente: `SET_AUTO:1` responde `ERR:SAFE_STARTUP` hasta `CMD:SAFE_RELEASE`.
2. Relés inicializados **OFF**; sin hardware, usar `sim on` por USB para pruebas lógicas (RTC “virtual” en STS).
3. Automatización AUTO exige RTC real **o** simulación (`sim on`).

## Compilar (PlatformIO)

```text
cd c:\e-Weed\controller_uno
pio run -e mega2560
```

## Cargar en Mega 2560 Pro Mini

1. Cable USB al CH340/USB del Pro Mini (o programador ISP si aplica).
2. Elegir placa **Arduino Mega 2560** en IDE / `board = megaatmega2560` en PIO.
3. Puerto COM correcto: `pio run -e mega2560 -t upload` (o carga desde Arduino IDE con los `.cpp` del sketch `controller_uno_cli/eweed_uno/` sincronizados).

## UART con ESP32

- **USB (`Serial`)**: 115200 — monitor y CLI local.
- **HMI (`Serial1`)**: 115200 — RX1 pin **19**, TX1 pin **18** (Mega estándar); cruzar TX↔RX con el ESP32.

## Checklist primer encendido

- [ ] Ningún relé conectado a carga fuerte en la primera prueba (o carga mínima).
- [ ] Verificar polaridad de módulos de relé (`kRelayActiveHigh` en `config_pins.h`).
- [ ] Comprobar serigrafía de la Pro Mini frente a `config_pins.h`.
- [ ] Sin sensores: `sim on` por USB, `SAFE_RELEASE`, `SET_MAINT:0`, revisar `STS` en Serial1.
- [ ] Con RTC: quitar sim, confirmar `RTC=1` y `CLK` ISO en `STS`.
- [ ] Solo entonces `SET_AUTO:1` y prueba de lazo con depósito real.

## Módulos (resumen breve)

- **Sensores**: medias ADC, contadores de invalidez, nivel inconsistente (mín+mán estable), opción simulación.
- **Actuadores**: nunca A+B a la vez ni pH++pH-; nivel mínimo corta dosificación; nivel máximo corta llenado.
- **Automatización**: ciclo pH → recirc → estabilización → TDS (A → recirc → B → recirc → estabilización) → luz/vent → `STS`.
- **UART**: misma gramática que la HMI actual; `MANUAL_LOCKED` si `OUT` en AUTO sin mantenimiento.
