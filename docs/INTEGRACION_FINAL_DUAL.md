# Integracion Final Dual (ESP32 COM4 + UNO R4 COM5)

Fecha: 2026-04-04

## Estado ESP32 (COM4)

- Build: OK
- Upload: OK
- UI LVGL: estable
- LittleFS: monta correctamente
- Polling UART: activo (`TX: CMD:GET_STATUS`)

Cambios aplicados:

- `src/uart_comm.cpp`
  - Parser dual de entrada:
    - `UART2` (GPIO16/GPIO17)
    - `UART0` (canal debug/USB)
  - Log de origen de paquete: `RX[UART2]` / `RX[UART0]`
  - Estados ACK/ERR alineados a UI:
    - `CONFIRMADO: ...`
    - `ERROR: ...`
- `src/uart_comm.h`
  - Buffers y flags separados para cada transporte serial.

## Estado UNO R4 (COM5)

- PlatformIO del UNO sigue bloqueándose en core USB (`USB.cpp.o` / `usbtmc_device.c.o`) en este entorno.
- Workaround estable aplicado con `arduino-cli`:
  - Compilación: OK
  - Upload en COM5: OK

Script operativo:

- `tools/uno_r4_build_upload.ps1`
  - Compila desde `controller_uno/src/main.cpp`
  - Sincroniza a `controller_uno_cli/eweed_uno/eweed_uno.ino`
  - Carga a COM5 con `arduino-cli`

## Evidencia UART real

### Evidencia de TX ESP32 -> UNO (OK)

En COM5 se observa recepción constante:

- `RX1: TX: CMD:GET_STATUS`
- `RX1: [HB] up=...`

### Evidencia de respuesta UNO (genera STS/ACK) (OK)

En COM5 se observa emisión:

- `STS_USB:STS:PH=...;...`
- `ACK: REQ_STATUS`

### Evidencia de RX UNO -> ESP32 (FALLA EN CABLE DIRECTO)

En COM4 no aparecen paquetes `RX[UART2]: STS...` ni `RX[UART0]: STS...` durante captura prolongada.

## Prueba de control (software) del parser ESP32 (OK)

Se inyectó por COM4:

- `STS:...`
- `ACK:GET_STATUS`

Resultado en COM4:

- `RX[UART0]: STS:...`
- `RX[UART0]: ACK:GET_STATUS`

Conclusión:

- El parser y la actualización de estado del ESP32 **sí funcionan**.
- El problema remanente es de **retorno físico UNO->ESP32** (ruta RX).

## Bloqueador exacto (NO-GO actual)

No existe retorno UART efectivo desde UNO hacia ESP32 en hardware directo.

Diagnóstico más probable:

1. cable de retorno ausente o mal conectado (`UNO D1/TX -> ESP32 RX`),
2. pin de retorno conectado a GPIO distinto al esperado,
3. falta de adaptación de nivel 5V -> 3.3V en retorno UNO->ESP32,
4. GND común intermitente.

## Criterio GO pendiente

Para pasar a GO final deben observarse simultáneamente en COM4:

1. `TX: CMD:GET_STATUS`
2. `RX[UART2]: STS:...` o `RX[UART0]: STS:...`
3. `RX[UART2]: ACK:...` o `RX[UART0]: ACK:...`

Mientras eso no ocurra: **NO-GO por bloqueo físico de enlace RX**.

