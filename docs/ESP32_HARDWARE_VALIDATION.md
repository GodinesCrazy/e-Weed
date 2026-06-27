# ESP32 Hardware Validation (LVGL + LittleFS + UART)

Fecha: 2026-04-03  
Placa: ESP32-2432S028  
Puerto usado: COM4  
Firmware base: `env:esp32dev` (PlatformIO)

## 1. Estado de conexión y arranque

- Upload firmware: **OK**
- BuildFS: **OK**
- UploadFS: **OK**
- Boot estable: **SI**
- Resets/panic en la corrida actual: **NO**

Logs clave observados:

- `SETUP: done`
- Heartbeat continuo: `[HB] up=... free_heap=273000`
- Sin reinicios durante captura serial prolongada (55s)

## 2. Estado LittleFS y assets

LittleFS monta correctamente:

- `LittleFS mount: OK`
- `FS READY: YES`
- Listado recursivo de `/assets/*`: **OK**
- Check de assets esperados: **FOUND en todos**

### Tabla de rutas (real)

| Ruta física proyecto | Ruta esperada en LittleFS | Ruta usada por UI/LVGL |
|---|---|---|
| `data/assets/logo/logo_main_240x120.png` | `/assets/logo/logo_main_240x120.png` | `S:/assets/logo/logo_main_240x120.png` |
| `data/assets/backgrounds/splash_screen_320x240.png` | `/assets/backgrounds/splash_screen_320x240.png` | `S:/assets/backgrounds/splash_screen_320x240.png` |
| `data/assets/menu/menu_stages_64.png` | `/assets/menu/menu_stages_64.png` | `S:/assets/menu/menu_stages_64.png` |
| `data/assets/stages/stage_1_64.png` | `/assets/stages/stage_1_64.png` | `S:/assets/stages/stage_1_64.png` |
| `data/assets/icons/icon_ph_32.png` | `/assets/icons/icon_ph_32.png` | `S:/assets/icons/icon_ph_32.png` |
| `data/assets/status/status_alert_32.png` | `/assets/status/status_alert_32.png` | `S:/assets/status/status_alert_32.png` |
| `data/assets/navigation/nav_home_32.png` | `/assets/navigation/nav_home_32.png` | `S:/assets/navigation/nav_home_32.png` |

Resultado: **sin mismatch de rutas**.

## 3. Estado PNG/LVGL

- `LV_USE_PNG: 1`
- Decoder custom registrado: `PNG decoder init: OK`
- En hardware real, PNG grande sigue fallando por RAM:
  - `Out of memory for PNG buffer`
  - `Decode error 83: memory allocation failed`
- Fallback embebido activo:
  - `EMBEDDED FALLBACK ACTIVE`

Resultado práctico: UI continúa visible/usando assets embebidos cuando PNG runtime no alcanza memoria.

## 4. Estado UART real con UNO R4

Estado actual:

- ESP32 transmite polling: `TX: CMD:GET_STATUS` (continuo)
- Respuesta válida desde UNO (`STS/ACK/ERR/ALM`): **NO detectada en esta validación**
- Indicador de enlace: `UART: sin enlace valido (esperando STS/ACK)`

Conclusión UART:

- Enlace **no validado** extremo a extremo aún.
- UI queda operativa con estado `UART OFF` y sin congelamientos.

## 5. Cambios aplicados en esta iteración

1. Estabilidad runtime
- Se eliminó causa del panic `LoadProhibited` (uso de `%f` en `lv_label_set_text_fmt` de LVGL).
- Formateo de flotantes migrado a texto seguro sin `lv_printf` de float.

2. UI en español y legibilidad
- Ajustes de texto/footers a español.
- Ajustes de layout para reducir sobreposición texto/icono.
- Etapas en español y textos largos ajustados.

3. Actuadores/submenús
- Tarjetas de actuadores marcadas como clickeables reales.
- Comandos actualizados al protocolo del UNO:
  - modo: `CMD:SET_MODE:AUTO|MANUAL|MAINT`
  - salidas: `CMD:OUT:<KEY>:<0|1>`
- Botones de Control/Mantención fuerzan modo correspondiente.

4. UART robustez
- Se retiró autobaud agresivo (fuente de inestabilidad de enlace).
- Poll no bloqueante mantenido (`CMD:GET_STATUS`).

## 6. Validación de performance

- Heap estable en logs (~273000 bytes libres durante ejecución)
- Sin reinicios en ventana de prueba prolongada
- UI no bloqueante (task UI + task UART activas)

## 7. UI real vs simulada

- UI render: **REAL (LVGL sobre TFT)**
- Assets: **LittleFS + fallback embebido**
- Sensores en vivo por UART: **NO (aún sin respuesta del UNO en esta prueba)**
- Mocks forzados: **NO** (cuando no hay telemetría se muestra `--/N/D`)

## 8. Problemas detectados y solución aplicada

Problemas:
- Panic por formato float en labels LVGL.
- Actuadores sin click efectivo.
- Comandos UART de UI no alineados al protocolo real del UNO.

Soluciones:
- Formateo seguro sin `%f` en LVGL.
- Tarjetas de actuadores clickeables + estado visual.
- Cambio de comandos a `SET_MODE` y `OUT`.

## 9. GO / NO-GO

- UI/LVGL/LittleFS: **GO**
- Estabilidad firmware ESP32: **GO**
- Integración UART real ESP32↔UNO: **NO-GO** (falta validar RX real desde UNO)

Requisito para pasar a GO total:
- Recibir y parsear `STS:` real del UNO de forma estable.

