# e-Weed — Arquitectura profesional

## Objetivo

e-Weed es una plataforma de automatización hidropónica/indoor con arquitectura de dos controladores:

- **ESP32-2432S028 / CYD:** HMI táctil, UI LVGL, WiFi, API local, almacenamiento y supervisión.
- **Arduino UNO R4 Minima:** controlador I/O oficial para sensores, relés, RTC, alarmas e interlocks físicos.

La regla principal del diseño es que el **UNO R4 debe ser la autoridad de seguridad física**. La HMI puede solicitar acciones, pero el controlador I/O decide si una salida puede activarse.

## Flujo de control

```text
Usuario / HMI touch
        │
        ▼
ESP32 HMI ── CMD UART ──► UNO R4 I/O
        ▲                 │
        │                 ▼
        ◄── STS/ACK/ERR ── Sensores + relés + alarmas
```

## Responsabilidades del ESP32

- Renderizar UI touch LVGL.
- Mostrar telemetría y alarmas.
- Enviar comandos UART al UNO R4.
- Servir API REST local.
- Persistir ajustes HMI.
- Mostrar estado de conectividad, salud y sensores.
- No debe asumir control físico directo de relés críticos.

## Responsabilidades del UNO R4

- Leer pH, TDS/EC, temperatura de agua, temperatura/humedad ambiental y nivel.
- Controlar relés de bombas, pH, recirculación, luz, ventilación y buzzer.
- Aplicar interlocks antes de activar salidas.
- Mantener modo seguro inicial.
- Rechazar salidas inseguras.
- Emitir telemetría `STS`, confirmaciones `ACK`, errores `ERR` y alarmas `ALM`.

## Estados críticos

El sistema debe protegerse especialmente ante:

- Nivel mínimo activo.
- Nivel máximo activo.
- Sonda pH inválida.
- Sensor TDS inválido.
- Temperatura de agua inválida.
- DHT sin lectura.
- RTC no disponible en modo automático.
- Intento de activar Nutriente A y B simultáneamente.
- Intento de activar pH+ y pH- simultáneamente.
- Timeout de llenado.

## Interlocks mínimos

- Si `levelMinActive == true`, bloquear nutrientes, pH, recirculación y dosificación crítica.
- Si `levelMaxActive == true`, bloquear llenado.
- Si Pump A se activa, Pump B debe apagarse antes.
- Si Pump B se activa, Pump A debe apagarse antes.
- Si pH+ se activa, pH- debe apagarse antes.
- Si pH- se activa, pH+ debe apagarse antes.
- En alarma activa, el controlador debe conservar estado seguro.

## Protocolo UART

### HMI → UNO R4

- `CMD:GET_STATUS`
- `CMD:SET_AUTO:0|1`
- `CMD:SET_MAINT:0|1`
- `CMD:SET_STAGE:n`
- `CMD:OUT:<KEY>:0|1`
- `CMD:CAL_PH_START`
- `CMD:CAL_PH_SAVE:<offset>:<slope>`
- `CMD:CAL_TDS_START`
- `CMD:CAL_TDS_SAVE:<factor>`
- `CMD:ACK_ALARM`

### UNO R4 → HMI

- `STS:<key=value;...>`
- `ACK:<payload>`
- `ERR:<payload>`
- `ALM:<payload>`

## Mapa de relés oficial

| Key | Función | Tipo |
|---|---|---|
| PA | Nutriente A | 12V |
| PB | Nutriente B | 12V |
| PHU | pH+ | 12V |
| PHD | pH- | 12V |
| REC | Recirculación | 12V |
| PIN | Llenado / agua IN | 12V |
| LUZ | Luz | 220V aislado |
| INT | Intractor | 220V aislado |
| EXT | Extractor | 220V aislado |
| BUZ | Buzzer / alarma | baja tensión |

## Seguridad eléctrica

Las cargas 220V deben quedar separadas físicamente de la electrónica de bajo voltaje. Usar gabinete, fusibles, borneras, cableado apropiado y módulos de relé/SSR dimensionados para la carga real.

## Criterio profesional

Antes de agregar nuevas funciones, el proyecto debe conservar:

1. Compilación limpia en ESP32.
2. Compilación limpia en UNO R4.
3. Interlocks físicos en UNO R4.
4. UI clara para modo AUTO/MANUAL/MANT.
5. Calibración guiada y verificable.
6. Documentación de pines y seguridad.
