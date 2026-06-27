# DUAL DEVICE - Auditoria Tecnica Inicial

Fecha: 2026-04-03  
Proyecto: e-Weed (ESP32 HMI + Arduino UNO R4 controlador)  
Hardware objetivo:
- ESP32-2432S028 en `COM4`
- Arduino UNO R4 Minima en `COM5`

---

## 1) Estado real del ESP32 (COM4)

### A. Archivos que conforman el firmware real ESP32

Nucleo funcional actual:
- `src/main.cpp`
- `src/hal_setup.cpp`
- `src/assets_fs.cpp`
- `src/data_model.cpp`
- `src/uart_comm.cpp`
- `src/ui/ui_manager.cpp`
- `src/generated_assets.cpp`
- `include/app_assets.h`
- `include/generated_assets.h`

### B. UI actualmente funcional

Pantallas implementadas y navegables:
- Splash
- Menu principal
- Sensores
- Etapas
- Actuadores
- Alertas

Interacciones activas:
- Botones de navegacion (atras / home / siguiente)
- Cambio de etapa (selector de 5 etapas)
- Toggle de actuadores (tarjetas clickeables)
- Toggle de modo AUTO/MANUAL

### C. Datos placeholder / dependencias de telemetria

Comportamiento actual:
- Si no llega telemetria UART (`telemetry_live=false`): muestra `--` y `N/D`.
- No se inyectan mocks activos en ESP32.
- El estado visual de actuadores/etapa puede cambiar localmente por interaccion UI y luego esperar confirmacion del UNO por UART.

### D. UART actual en ESP32

Implementacion:
- `HardwareSerial(2)` a `115200`, pines `RX=16`, `TX=17`.
- Poll no bloqueante cada ~1200 ms: `CMD:GET_STATUS`.
- Timeout de enlace ~3500 ms.
- Parser de lineas (`\n`) con validacion basica de paquete.

Paquetes aceptados desde UNO:
- `STS:...`
- `ACK:...`
- `ERR:...`
- `ALM:...`

Paquetes **no** soportados hoy en parser ESP32:
- `NACK:...`
- `SNS:...`

### E. Comandos que transmite hoy el ESP32

Detectados en firmware:
- `CMD:GET_STATUS`
- `CMD:SET_STAGE:<id>`
- `CMD:SET_MODE:AUTO|MANUAL|MAINT`
- `CMD:OUT:<KEY>:<0|1>`

No transmitidos aun:
- `CMD:GET_SENSORS`
- `CMD:RESET_ALERTS`
- `CMD:CALIBRATE:<TYPE>`
- `CMD:SAVE_CONFIG`

### F. Estados visuales actuales relevantes

- Enlace UART: `UART OK / UART OFF`
- Modo: `AUTO / MANUAL`
- Actuadores: `ENC / APAG` + icono estado
- Alerta: `Sistema estable / Alerta activa`
- Etapa activa: destacada en selector

### G. Riesgos tecnicos ESP32

1. PNG runtime grande sigue fallando por RAM (`Out of memory`), mitigado por fallback embebido.
2. Formato de protocolo parcial (no contempla `NACK`/`SNS`).
3. Estado UI optimista para actuadores (toggle local) antes de ACK real del UNO.
4. Posible riesgo electrico si UNO TX (5V) entra directo a RX ESP32 sin level-shifter.

---

## 2) Estado real del Arduino UNO R4 (COM5)

### A. Firmware utilizable existente en repo

Existe firmware completo en:
- `controller_uno/src/main.cpp`

Capacidades implementadas en ese firmware:
- Parser UART con prefijo `CMD:`
- Estado global (modo, alarmas, etapa)
- Scheduler no bloqueante con `millis()`
- Logica de seguridad/interlocks
- Manejo de relays
- Telemetria periodica `STS`
- `ACK` y `ERR`
- Mensajes `ALM`

### B. Parser UART implementado

Comandos soportados por firmware UNO en repo:
- `GET_STATUS` / `REQ_STATUS`
- `ACK_ALARM`
- `SET_STAGE:<id>`
- `SET_MODE:AUTO|MANUAL|MAINT|IDLE`
- `OUT:<KEY>:<0|1>`

### C. Sensores / actuadores

Sensores reales actualmente leidos:
- Nivel minimo y maximo (digital)
- RTC DS3231 por I2C

Sensores analogicos (pH/EC/temperatura/humedad):
- En firmware actual quedan en demo desactivado (`kEnableDemoTelemetry=false`) => valores no reales.

Actuadores:
- 9 salidas relay definidas y gestionadas.

### D. Hallazgo real en hardware COM5

Monitor serial real en `COM5` muestra:
- `Inicializando la SD card...Fallo en la inicialización.`

Conclusión:
- El UNO conectado en `COM5` **NO** está corriendo el firmware hidroponico del repo en este momento (esta corriendo otro sketch).

---

## 3) Estado real del enlace ESP32 <-> UNO

### A. Compatibilidad de protocolo (codigo vs codigo)

Compatibilidad actual:
- ESP32 envía comandos que UNO del repo entiende (`GET_STATUS`, `SET_STAGE`, `SET_MODE`, `OUT`).
- UNO del repo responde `STS/ACK/ERR/ALM`, que ESP32 parsea.

Inconsistencias para version final objetivo:
- Objetivo final pide `NACK` y `SNS`; parser ESP32 actual no los maneja.
- Objetivo final pide comandos extra (`GET_SENSORS`, `RESET_ALERTS`, `CALIBRATE`, `SAVE_CONFIG`) no implementados end-to-end.

### B. Causa raiz probable de “ESP32 transmite pero no recibe”

Con evidencia actual, causa principal:
1. UNO en COM5 tiene firmware incorrecto cargado (no el de `controller_uno/src/main.cpp`).

Posibles causas adicionales a validar en siguiente fase:
2. Cableado TX/RX no cruzado o GND comun ausente.
3. Nivel logico 5V->3.3V sin conversion en RX del ESP32.
4. UNO `Serial1` en pines D0/D1 sin conexion fisica a ESP32 16/17.

---

## 4) Matriz IMPLEMENTADO / PARCIAL / FALTANTE

| Item | Estado | Evidencia |
|---|---|---|
| ESP32 arranque estable | IMPLEMENTADO | Boot + heartbeat continuo, sin panic actual |
| LittleFS + assets esperados | IMPLEMENTADO | Startup report con `FOUND` completo |
| UI LVGL multi-pantalla | IMPLEMENTADO | 6 pantallas + navegacion |
| Fallback embebido de imagenes | IMPLEMENTADO | `EMBEDDED FALLBACK ACTIVE` |
| UART TX ESP32 | IMPLEMENTADO | `TX: CMD:GET_STATUS` periodico |
| UART RX ESP32 real desde UNO | FALTANTE | sin `RX: STS/ACK/ERR/ALM` en pruebas |
| Firmware UNO hidroponico cargado en COM5 | FALTANTE | monitor COM5 muestra sketch SD ajeno |
| Parser UART UNO en repo | IMPLEMENTADO | `processCommand(...)` completo |
| Sensores reales completos UNO (pH/EC/temp/hum) | FALTANTE | actualmente demo off y sin adquisicion real integrada |
| Protocolo extendido final (`NACK`,`SNS`, comandos extra) | FALTANTE | no implementado extremo a extremo |

---

## 5) Bloqueadores tecnicos actuales

1. UNO correcto no cargado en COM5.
2. Enlace fisico UART no validado con cableado real (TX/RX/GND/level shifting).
3. Sensores principales aun no integrados como lectura real en firmware UNO actual.
4. Toolchain `controller_uno` no compilada exitosamente en este entorno durante esta auditoria (timeouts), pendiente resolver en fase de implementacion.

---

## 6) GO / NO-GO inicial

Estado inicial por subsistema:
- ESP32 UI/FS/estabilidad: **GO**
- UNO en hardware real (COM5): **NO-GO**
- Enlace UART extremo a extremo: **NO-GO**

GO global inicial del sistema dual: **NO-GO**.

Condición minima para pasar a GO en integración:
1. Cargar firmware UNO correcto en COM5.
2. Validar físicamente UART bidireccional (STS + ACK reales visibles en ESP32).

