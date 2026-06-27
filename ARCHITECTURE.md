# Arquitectura del Sistema e-Weed

## Objetivo
Definir una arquitectura clara para el sistema hidropónico usando el hardware ya identificado y el firmware HMI existente en este repositorio.

Este repositorio implementa la capa HMI en la placa `ESP32-2432S028`. El controlador de proceso propuesto es el `Arduino UNO R4 Minima`, que concentra sensores, horarios y accionamiento.

## Resumen de roles

### ESP32-2432S028
- Interfaz gráfica táctil.
- Visualización de variables en tiempo real.
- Menús de configuración, alarmas y mantenimiento.
- Envío de comandos al controlador principal por UART.
- No debe asumir control directo de bombas o relés en la arquitectura base.

### Arduino UNO R4 Minima
- Controlador principal del proceso.
- Lectura de sensores.
- Gestión de horarios con RTC.
- Lógica de seguridad.
- Accionamiento de relés, buzzer y salidas del sistema.
- Publicación periódica del estado hacia el ESP32 por UART.

### DS3231
- Fuente de fecha y hora persistente.
- Mantiene programación incluso si el sistema se reinicia o corta energía.

### Módulo de 16 relés
- Conmutación de cargas de potencia o maniobra.
- Se recomienda usar solo los canales necesarios y reservar otros para expansión.

### Sensores de nivel
- Protección contra vacío, desborde o estados inválidos del depósito.
- Deben cablearse al UNO R4 como entradas de seguridad.

## Arquitectura lógica

```text
                 +----------------------+
                 |  ESP32-2432S028      |
                 |  HMI táctil LVGL     |
                 |  Dashboard / Menús   |
                 +----------+-----------+
                            |
                            | UART 115200
                            |
                 +----------v-----------+
                 |  Arduino UNO R4      |
                 |  Lógica de control   |
                 |  Seguridad / Estados |
                 +---+--------+-----+---+
                     |        |     |
                  I2C|        |     |Salidas digitales
                     |        |     |
              +------v--+  +--v--+  +----------------------+
              | DS3231  |  |Nivel|  | Módulo 16 relés      |
              | RTC     |  |min/max| | Bombas / luces / etc |
              +---------+  +------+  +----------------------+
                                      |
                                      +--> Buzzer / cargas auxiliares
```

## Arquitectura eléctrica propuesta

### Alimentación
- Entrada principal: `12V` o `24V` según la instalación final.
- Convertidor `DC-DC TOBSUN 12/24V -> 5V 15A`:
  - Alimenta `UNO R4`, `ESP32`, `DS3231`, sensores y lógica de relés.
  - Debe compartir `GND` común con todos los módulos de control.

### Reglas básicas de alimentación
- Unificar tierras entre `ESP32`, `UNO R4`, relés y sensores.
- Separar, si es posible, la alimentación de lógica y la de cargas inductivas.
- Las cargas con motor deben tener protección contra ruido eléctrico.
- Si los relés conmuntan bombas DC pequeñas con mucha frecuencia, conviene migrar esas cargas a MOSFETs en una etapa posterior.

## Distribución funcional recomendada

### Control en UNO R4
- Adquisición de sensores:
  - `DS3231` por `I2C`.
  - `Sensor de nivel mínimo`.
  - `Sensor de nivel máximo`.
  - Futuros sensores de pH, TDS, temperatura de agua, temperatura/HR ambiente.
- Accionamiento:
  - Luz.
  - Intractor.
  - Extractor.
  - Recirculación.
  - Bomba de llenado.
  - Bomba nutriente A.
  - Bomba nutriente B.
  - Dosificación pH up.
  - Dosificación pH down.
  - Buzzer.
- Lógica:
  - Horarios por etapa.
  - Modo automático.
  - Modo mantenimiento.
  - Alarmas.
  - Interbloqueos por nivel.

### Interfaz en ESP32
- Pantallas:
  - `Idle`
  - `Main Menu`
  - `Stages`
  - `Dashboard`
  - `Calibration`
  - `Settings`
  - `Alarms`
  - `Maintenance`
- Funciones:
  - Mostrar variables del proceso.
  - Cambiar etapa activa.
  - Activar acciones manuales.
  - Confirmar alarmas.
  - Ajustar consignas.

## Asignación inicial de relés

El modelo actual del HMI ya contempla estas salidas:

| Canal | Carga propuesta | Estado existente en firmware |
|---|---|---|
| R1 | Luz principal | `state_light` |
| R2 | Intractor | `state_intractor` |
| R3 | Extractor | `state_extractor` |
| R4 | Bomba recirculación | `state_recirculation` |
| R5 | Bomba de llenado | `state_pump_in` |
| R6 | Bomba nutriente A | `state_pump_a` |
| R7 | Bomba nutriente B | `state_pump_b` |
| R8 | Dosificación pH up | `state_ph_up` |
| R9 | Dosificación pH down | `state_ph_down` |
| R10 | Buzzer o sirena | reservado |
| R11-R16 | Reserva | expansión futura |

## Comunicación UART propuesta

La implementación actual del ESP32 ya espera paquetes de texto terminados en salto de línea.

### Del UNO R4 al ESP32
Estado periódico:

```text
STS:PH=5.82;TDS=640;TW=20.1;TA=22.4;HA=61.2;NMIN=0;NMAX=1;LUZ=1;INT=0;EXT=1;REC=1;ETAPA=2;ALARM=0;ACT=Recirculando
```

Campos ya soportados por el HMI:
- `PH`
- `TDS`
- `TW`
- `TA`
- `HA`
- `NMIN`
- `NMAX`
- `LUZ`
- `INT`
- `EXT`
- `REC`
- `ETAPA`
- `ALARM`
- `ACT`

### Del ESP32 al UNO R4
Comandos sugeridos:

```text
CMD:SET_STAGE:2
CMD:SET_MODE:AUTO
CMD:SET_MODE:MANUAL
CMD:ACK_ALARM
CMD:OUT:REC:1
CMD:OUT:REC:0
CMD:OUT:PUMP_A:1
CMD:OUT:PUMP_A:0
CMD:REQ_STATUS
```

### Respuestas del UNO R4
```text
ACK:SET_STAGE:2
ERR:INVALID_STAGE
ALM:Nivel minimo activo
```

## Reglas de seguridad recomendadas

### Nivel de agua
- Si `NMIN=1` indicando nivel crítico:
  - Bloquear dosificación.
  - Bloquear recirculación si existe riesgo de trabajo en seco.
  - Activar alarma visual y buzzer.
- Si `NMAX=1`:
  - Bloquear bomba de llenado.
  - Registrar evento.

### Dosificación
- Nunca activar simultáneamente `PUMP_A` y `PUMP_B`.
- Nunca dosificar `PH_UP` y `PH_DOWN` al mismo tiempo.
- Agregar tiempos muertos entre dosificaciones.

### Comunicación
- Si el ESP32 deja de recibir `STS` durante una ventana definida:
  - Mostrar `comunicación perdida`.
  - Mantener el control en UNO R4 sin depender del HMI.

### Filosofía general
- El `UNO R4` debe seguir operando de forma segura aunque el `ESP32` se reinicie, se desconecte o falle.
- El `ESP32` es interfaz, no punto único de fallo del proceso.

## Máquina de estados del sistema

### Estados de operación
- `BOOT`
- `IDLE`
- `AUTO`
- `MANUAL`
- `ALARM`
- `MAINTENANCE`

### Flujo recomendado
1. `BOOT`: inicializa RTC, IO, sensores y relés en estado seguro.
2. `IDLE`: espera confirmación de sensores y hora válida.
3. `AUTO`: ejecuta horarios, lectura y control normal.
4. `MANUAL`: habilita maniobras desde HMI con bloqueos de seguridad.
5. `ALARM`: desactiva salidas sensibles y exige reconocimiento.
6. `MAINTENANCE`: permite pruebas controladas sin automatismos.

## Etapas de cultivo como capa de configuración

El HMI ya maneja `active_stage` y objetivos por etapa. La recomendación es tratar las etapas como perfiles de consigna:
- `0`: germinación / inicio
- `1`: crecimiento temprano
- `2`: crecimiento
- `3`: transición
- `4`: floración o etapa final

Cada etapa puede definir:
- rango de pH objetivo
- TDS objetivo
- ventanas horarias de iluminación
- tiempos de recirculación
- límites de alarma

## Mapeo con el firmware actual

La estructura actual del repo ya refleja esta separación:
- [README.md](C:\e-Weed\README.md): define al ESP32 como HMI por UART.
- [src/main.cpp](C:\e-Weed\src\main.cpp): divide UI y UART en tareas FreeRTOS.
- [src/uart_comm.cpp](C:\e-Weed\src\uart_comm.cpp): implementa el parser de `STS`, `ACK`, `ERR`, `ALM`.
- [src/data_model.h](C:\e-Weed\src\data_model.h): ya contiene variables de proceso y salidas del sistema.

## Pendientes para cerrar diseño

Antes de cablear o escribir el firmware del UNO conviene confirmar:
- Tensión real de trabajo de cada bomba.
- Si el módulo de 16 relés es activo en bajo o activo en alto.
- Tipo exacto de sensor de nivel.
- Si el buzzer se manejará directo o por transistor.
- Si pH y TDS serán analógicos dedicados o módulos UART/I2C.
- Si el ESP32 además tendrá Wi-Fi para telemetría o quedará solo como HMI local.

## Decisión recomendada

La arquitectura base a implementar es:
- `ESP32-2432S028` como HMI local.
- `UNO R4 Minima` como controlador principal.
- `DS3231` conectado al UNO.
- Sensores de nivel conectados al UNO.
- Relés conectados al UNO.
- UART como enlace HMI-control.

Es la opción más consistente con el firmware actual y la más robusta para operar aunque la pantalla falle.
