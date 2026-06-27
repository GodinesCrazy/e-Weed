# Informe Completo ESP32-2432S028 (e-Weed)

Fecha de actualización: 2026-04-07

## Objetivo

Completar la depuración visual y de comunicación del HMI (LVGL + LittleFS + UART), manteniendo la UI existente y validando en hardware real conectado por `COM4`.

## Actualización Assets (sprite sheet)

- Se rehizo el pipeline de extracción usando la hoja maestra en `reference/master_assets_sheet.png`.
- Se exportaron los 32 assets requeridos con nombre exacto en `data/assets/*.png`.
- Se conservaron copias legacy en `data/assets/<grupo>/` para compatibilidad.
- Se actualizó `include/app_assets.h` para usar rutas canónicas `S:/assets/<archivo>.png`.
- Se actualizó `src/assets_fs.cpp` con check de assets esperados en raíz.
- Se regeneró embedding LVGL desde `tools/generate_embedded_assets.py` con alias canónico + legacy.
- Validación ejecutada:
  - `platformio run`: **OK**
  - `platformio run --target buildfs`: **OK**

## Trabajo realizado

### 1) Pipeline visual y estabilidad

- Se validó montaje LittleFS en arranque: `LittleFS mount: OK`.
- Se agregó reporte recursivo de `/assets/*` con tamaño por archivo.
- Se verificó check completo de assets esperados (todos `FOUND`).
- Se confirmó que las rutas usan mapeo correcto:
  - Proyecto: `data/assets/...`
  - FS: `/assets/...`
  - LVGL: `S:/assets/...`

### 2) Causa raíz del crash

Se detectó `Guru Meditation (LoadProhibited)` en:

- `lv_label_set_text_fmt` dentro de `UiManager::update`.

La causa fue uso de formato flotante en `lv_label_set_text_fmt` con configuración LVGL sin soporte float en `lv_printf`.

Corrección aplicada:

- Se eliminó `%f` en labels LVGL.
- Se migró a formateo de texto seguro y asignación con `lv_label_set_text`.

Resultado:

- Sin reinicios en captura serial prolongada (55s).
- Heartbeat estable y heap estable.

### 3) Ajustes UI solicitados

- Menú y textos en español.
- Nombres de etapas ajustados:
  1. Germinacion y Plantula
  2. Etapa Vegetativa
  3. Pre-Floracion
  4. Floracion
  5. Maduracion y lavado de Raices
- Reducción de sobreposición texto/iconos en tarjetas de sensores y actuadores.
- Ajuste de posiciones verticales en menú para evitar recorte inferior.
- Realce de botones mantenido (`pressed/checked`) con transición.

### 4) Submenús/actuadores

Se corrigió comportamiento de actuadores:

- Tarjetas marcadas como clickeables reales.
- Toggle local visible + estado checked.
- Comandos adaptados al protocolo real del UNO:
  - `CMD:SET_MODE:AUTO|MANUAL|MAINT`
  - `CMD:OUT:<KEY>:<0|1>`
- Botones de `Control` y `Mantencion` fuerzan modo UART correspondiente.

### 5) Estado PNG y fallback

- `LV_USE_PNG: 1` y decoder custom activo.
- PNG grandes siguen fallando por RAM en runtime (`Out of memory` / `Decode error 83`).
- Fallback embebido activo, evitando pantalla vacía.

## Software cargado en ESP32

- Plataforma: `espressif32 6.13.0`
- Framework: Arduino ESP32 `3.20017.241212+sha.dcc1105b`
- LVGL: `8.4.0`
- TFT_eSPI: `2.5.43`
- Touch: `XPT2046_Touchscreen v1.4`
- Filesystem: LittleFS

Último build cargado:

- RAM: ~26.4%
- Flash: ~83.2%
- `build`: OK
- `buildfs`: OK
- `uploadfs`: OK
- `upload`: OK

## Archivos modificados en esta iteración

- `src/ui/ui_manager.cpp`
- `src/uart_comm.cpp`
- `src/uart_comm.h`
- `src/data_model.h`
- `docs/ESP32_HARDWARE_VALIDATION.md`
- `INFORME_COMPLETO_ESP32.md`

## Estado de integración UART real (ESP32 ↔ UNO)

Estado actual en prueba real:

- ESP32 transmite polling: `TX: CMD:GET_STATUS`.
- UNO recibe ese tráfico (evidencia en COM5: `RX1: TX: CMD:GET_STATUS`).
- UNO responde `STS` y `ACK` (evidencia en COM5: `STS_USB:...` y `ACK: REQ_STATUS`).
- ESP32 quedó actualizado con parser dual (`UART2` + `UART0`) y se validó por inyección en COM4:
  - `RX[UART0]: STS:...`
  - `RX[UART0]: ACK:...`
- Enlace directo UNO->ESP32 sigue sin retorno visible en COM4, por lo que persiste bloqueo físico en la ruta RX.

Conclusión:

- Visual y estabilidad: **OK**
- Firmware UNO en COM5: **OK** (carga estable por `arduino-cli`)
- Enlace UART extremo a extremo por cable: **NO-GO (bloqueo físico RX)**

## Comandos operativos

```powershell
# Compilar
C:\Users\ivanm\.platformio\penv\Scripts\pio.exe run -j 1

# Filesystem
C:\Users\ivanm\.platformio\penv\Scripts\pio.exe run --target buildfs
C:\Users\ivanm\.platformio\penv\Scripts\pio.exe run --target uploadfs --upload-port COM4

# Firmware
C:\Users\ivanm\.platformio\penv\Scripts\pio.exe run --target upload --upload-port COM4

# Monitor
C:\Users\ivanm\.platformio\penv\Scripts\pio.exe device monitor --port COM4 --baud 115200
```

## UNO R4: workaround estable para bloqueo `USB.cpp.o` / `usbtmc_device.c.o`

En este equipo, el build UNO por PlatformIO se sigue atascando en archivos del core USB.
Para evitar bloqueo, se valida y carga UNO con `arduino-cli` usando script del proyecto:

```powershell
# Solo compilar UNO
powershell -ExecutionPolicy Bypass -File .\tools\uno_r4_build_upload.ps1 -CompileOnly

# Compilar + subir a COM5
powershell -ExecutionPolicy Bypass -File .\tools\uno_r4_build_upload.ps1 -Port COM5
```

Validacion real ejecutada:

- Compilacion UNO: **OK**
- Upload UNO en `COM5`: **OK**
- Evidencia serial UNO: recibe `CMD:GET_STATUS` y responde `STS` + `ACK`.
