# e-Weed HMI Professional v2.0

Plataforma hidropónica autónoma, inteligente y conectada basada en **ESP32-2432S028** (CYD) + **Arduino UNO R4**.

## Arquitectura del sistema

```
ESP32 (HMI + Cerebro)                     UNO R4 (Controlador I/O)
┌─────────────────────┐    UART 115200    ┌────────────────────┐
│  LVGL UI 320×240    │◄────────────────►│  Sensores (DHT22,  │
│  WiFi AP/STA        │   GPIO16/17       │  PH-4502C, DS3231) │
│  REST API :80       │                   │  9 Relés            │
│  Automation Engine  │                   │  Nivel agua         │
│  Data Logger        │                   │  Buzzer             │
└─────────────────────┘                   └────────────────────┘
         │
    WiFi / HTTP
         │
   App Móvil / Browser
```

## Estructura del proyecto

```
src/
├── main.cpp                    # Entry point + 5 tareas FreeRTOS
├── hal_setup.cpp/.h            # TFT + Touch + LVGL init (320×240)
├── data_model.cpp/.h           # Modelo compartido thread-safe
├── uart_comm.cpp/.h            # Protocolo UART → UNO R4
├── assets_fs.cpp/.h            # LittleFS + PNG decoder
├── lv_conf.h                   # Config LVGL
├── generated_assets.cpp        # Assets embebidos (auto-generado)
├── automation/
│   ├── automation_engine.h
│   └── automation_engine.cpp   # Motor IA simple con histéresis
├── network/
│   ├── network_manager.h
│   └── network_manager.cpp     # WiFi + HTTP REST API
├── storage/
│   ├── storage_manager.h
│   └── storage_manager.cpp     # Ring buffer + CSV en LittleFS
└── ui/
    ├── ui_manager.h
    ├── ui_manager.cpp          # 11 pantallas LVGL profesionales
    ├── themes.h
    └── themes.cpp

include/
├── app_assets.h                # Rutas de assets S:/
└── generated_assets.h          # Descriptores de imágenes

controller_uno/                 # Subproyecto Arduino UNO R4
├── platformio.ini
└── src/main.cpp
```

## Pantallas de la interfaz

| # | Pantalla | Descripción |
|---|----------|-------------|
| 0 | Boot Test | Pantalla de diagnóstico |
| 1 | **Splash** | Animación de arranque → auto-avanza al Dashboard |
| 2 | **Dashboard** | Vista rápida 6 sensores + estado salud + navegación |
| 3 | Menú Principal | Grid 4×2 con acceso a todas las secciones |
| 4 | Sensores | Lectura detallada con rangos ideales por etapa |
| 5 | Etapas | 5 etapas de cultivo con perfiles configurables |
| 6 | Actuadores | Control manual/auto de 6 actuadores + selector de modo |
| 7 | Alertas | Centro de alarmas con ACK |
| 8 | Calibración | Ajuste pH (2 puntos) + offsets de sensores |
| 9 | Ajustes | Tema oscuro/claro + estadísticas UART |
| 10 | **WiFi/API** | Estado de red + documentación de endpoints |

## Tareas FreeRTOS

| Tarea | Core | Stack | Prioridad | Función |
|-------|------|-------|-----------|---------|
| UI | 1 | 10KB | 2 | LVGL rendering + actualización pantallas |
| UART | 0 | 4KB | 1 | Comunicación bidireccional con UNO R4 |
| Automation | 1 | 3KB | 1 | Motor de automatización con histéresis |
| Network | 0 | 8KB | 1 | WiFi + servidor HTTP REST |
| Storage | 1 | 4KB | 1 | Registro histórico en LittleFS |

## Motor de automatización

### Reglas con histéresis

| Parámetro | Acción si BAJO | Acción si ALTO | Histéresis |
|-----------|----------------|----------------|------------|
| pH | Dosificador alcalino (PHU) | Dosificador ácido (PHD) | ±0.15 |
| EC/TDS | Nutrientes A+B (PA, PB) | Diluir (PIN) | ±50 ppm |
| Temp. aire | -- | Ventiladores (INT, EXT) | 1.0°C |
| Humedad | Recirculación (REC) | -- | 3.0% |
| Nivel agua | Llenado (PIN) | Stop | Sensores min/max |

### Modos de operación

- **AUTO** (0): Automatización completa según perfil de etapa activa
- **MANUAL** (1): Sin intervención automática, solo monitoreo
- **HYBRID** (2): Automatización parcial (reservado para expansión)

### Niveles de salud del sistema

- **NORMAL** (verde): Todos los parámetros dentro de rango
- **WARNING** (amarillo): Algún parámetro fuera de rango ideal
- **ALERT** (rojo): Parámetro crítico o alarma activa

## API REST (WiFi)

### Conexión WiFi

**Modo por defecto:** Access Point

| Parámetro | Valor |
|-----------|-------|
| SSID | `e-Weed-AP` |
| Password | `eweed1234` |
| IP | `192.168.4.1` |

Si se configura SSID de red WiFi → se conecta como estación (STA) con fallback automático a AP.

### Endpoints

#### `GET /api/status`
Estado completo del sistema.

```json
{
  "ph": 5.82,
  "ec": 800,
  "temp_water": 21.5,
  "temp_air": 23.4,
  "humidity": 62.0,
  "level_min": false,
  "level_max": false,
  "health": 0,
  "stage": 1,
  "alarm": 0,
  "auto_mode": true,
  "maintenance": false,
  "uart": true,
  "wifi_ip": "192.168.4.1",
  "uptime": 3600,
  "heap": 180000
}
```

#### `GET /api/sensors`
Solo lecturas de sensores.

```json
{
  "ph": 5.82,
  "ec": 800,
  "temp_water": 21.5,
  "temp_air": 23.4,
  "humidity": 62.0,
  "level_min": false,
  "level_max": false
}
```

#### `GET /api/actuators`
Estado de todos los actuadores.

```json
{
  "light": true,
  "intractor": false,
  "extractor": true,
  "recirculation": false,
  "pump_in": false,
  "pump_a": false,
  "pump_b": false,
  "ph_up": false,
  "ph_down": false
}
```

#### `POST /api/actuators`
Control remoto de actuadores.

```json
{"actuator": "LUZ", "state": true}
```

Claves válidas: `LUZ`, `INT`, `EXT`, `REC`, `PIN`, `PA`, `PB`, `PHU`, `PHD`

#### `GET /api/history`
Últimos 50 registros históricos.

```json
{
  "records": [
    {"t": 300000, "ph": 5.82, "ec": 800, "tw": 21.5, "ta": 23.4, "ha": 62.0, "l": 0, "h": 0}
  ],
  "total": 288
}
```

#### `GET /api/config`
Configuración del motor de automatización.

```json
{
  "mode": 1,
  "ph_hysteresis": 0.15,
  "ec_hysteresis": 50,
  "temp_hysteresis": 1.0,
  "hum_hysteresis": 3.0,
  "min_dose_interval_ms": 30000,
  "dose_duration_ms": 3000,
  "eval_interval_ms": 2000
}
```

#### `POST /api/config`
Cambiar modo de automatización.

```json
{"mode": 0}
```

Valores: `0` = manual, `1` = auto, `2` = hybrid

### CORS

Todos los endpoints incluyen headers CORS (`Access-Control-Allow-Origin: *`) para consumo desde apps web/móvil.

## Ejemplo: consumo desde app móvil

### JavaScript / React Native / Expo

```javascript
const API_BASE = 'http://192.168.4.1';

// Leer estado
const response = await fetch(`${API_BASE}/api/status`);
const data = await response.json();
console.log(`pH: ${data.ph}  EC: ${data.ec}  Salud: ${data.health}`);

// Encender luz
await fetch(`${API_BASE}/api/actuators`, {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ actuator: 'LUZ', state: true })
});

// Cambiar a modo automático
await fetch(`${API_BASE}/api/config`, {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ mode: 1 })
});
```

### cURL

```bash
# Status
curl http://192.168.4.1/api/status

# Encender bomba
curl -X POST http://192.168.4.1/api/actuators \
  -H "Content-Type: application/json" \
  -d '{"actuator":"PIN","state":true}'

# Historial
curl http://192.168.4.1/api/history
```

## Perfiles de etapas de cultivo

| Etapa | pH | PPM | T°Agua | T°Aire | Humedad | Foto |
|-------|-----|-----|--------|--------|---------|------|
| 1. Germinación | 5.8-6.1 | 200-450 | 20-22°C | 22-26°C | 65-75% | 18/6 |
| 2. Vegetativa | 5.8-6.2 | 500-900 | 19-22°C | 22-28°C | 55-70% | 18/6 |
| 3. Pre-floración | 5.8-6.2 | 800-1100 | 19-21°C | 21-27°C | 45-60% | 12/12 |
| 4. Floración | 5.9-6.3 | 900-1300 | 18-21°C | 20-26°C | 40-50% | 12/12 |
| 5. Maduración | 5.8-6.2 | 150-400 | 18-20°C | 19-25°C | 40-50% | 12/12 |

## Compilación y flasheo

### Requisitos

- [PlatformIO](https://platformio.org/) (CLI o VSCode extension)
- ESP32-2432S028 (CYD) conectado por USB

### Comandos

```bash
# Compilar firmware
pio run

# Flashear firmware
pio run --target upload

# Compilar y subir filesystem (LittleFS con assets)
pio run --target buildfs
pio run --target uploadfs

# Monitor serial
pio device monitor
```

### Subproyecto UNO R4

```bash
cd controller_uno
pio run --target upload
```

## Conexiones físicas

Ver [conexiones.md](conexiones.md) para el diagrama de cableado completo.

### Resumen UART

| ESP32 | UNO R4 |
|-------|--------|
| GPIO16 (RX) | TX (Serial1) |
| GPIO17 (TX) | RX (Serial1) |
| GND | GND |

## Registro histórico

- **Buffer circular** en RAM: 288 registros (24h a intervalos de 5 min)
- **Persistencia**: Flush automático a `/history.csv` en LittleFS cada 60s
- **Formato CSV**: `timestamp,ph,tds,tw,ta,ha,level,health`
- **API**: Últimos 50 registros disponibles vía `GET /api/history`

## Recursos del sistema (compilado)

| Recurso | Uso | Disponible |
|---------|-----|------------|
| RAM | 36.3% (119KB) | 320KB |
| Flash | 83.8% (1.07MB) | 1.31MB |
| Tareas FreeRTOS | 5 | -- |
| Pantallas LVGL | 11 | -- |
| Endpoints API | 6 | -- |

## Licencia

Proyecto privado. Todos los derechos reservados.
