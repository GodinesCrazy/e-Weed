# e-Weed HMI Professional v3.0

Plataforma hidropónica autónoma, inteligente y conectada basada en **ESP32-2432S028 (CYD)** como HMI/cerebro principal y **Arduino UNO R4 Minima** como controlador oficial de I/O.

## Objetivo del proyecto

e-Weed busca centralizar el monitoreo, visualización y control de un sistema hidropónico/indoor mediante una arquitectura modular de dos controladores:

- **ESP32-2432S028 (CYD):** interfaz táctil, UI LVGL, WiFi, API REST, almacenamiento local y motor de automatización.
- **Arduino UNO R4 Minima:** lectura de sensores, control de relés, seguridad de actuadores y comunicación UART con el ESP32.

El diseño separa la interfaz y la lógica de alto nivel del control físico de entradas/salidas, facilitando mantenimiento, expansión y diagnóstico.

## Arquitectura del sistema

```text
ESP32 (HMI + Cerebro)                     UNO R4 Minima (Controlador I/O)
┌─────────────────────┐    UART 115200    ┌────────────────────┐
│  LVGL UI 320×240    │◄────────────────►│  Sensores          │
│  WiFi AP/STA        │   GPIO16/17       │  Relés             │
│  REST API :80       │                   │  Nivel agua        │
│  Automation Engine  │                   │  RTC               │
│  Data Logger        │                   │  Buzzer            │
└─────────────────────┘                   └────────────────────┘
         │
    WiFi / HTTP
         │
   App móvil / Browser
```

## Hardware oficial

### Controlador principal

- ESP32-2432S028 / CYD.
- Pantalla TFT ILI9341 320×240.
- Touch XPT2046.
- LittleFS para assets/configuración.
- WiFi AP/STA.

### Controlador I/O oficial

- Arduino UNO R4 Minima.
- Sensores conectados al controlador I/O.
- Relés de actuación.
- Comunicación UART hacia ESP32 HMI.

> Nota: el modelo oficial del controlador secundario es **Arduino UNO R4 Minima**. No se mantiene soporte oficial para Mega 2560 en esta versión del repositorio.

## Funciones principales

- Interfaz táctil LVGL.
- Dashboard local de sensores.
- Comunicación UART bidireccional ESP32 ↔ UNO R4.
- Motor de automatización con histéresis.
- Control de actuadores en modo manual/automático.
- Registro histórico en LittleFS.
- WiFi en modo AP/STA.
- API REST local.
- Configuración persistente.
- Alarmas y estados de salud del sistema.

## Variables y módulos contemplados

- pH.
- EC/TDS/PPM.
- Temperatura de agua.
- Temperatura/humedad ambiental.
- Nivel mínimo/máximo de agua.
- Bombas peristálticas 12V.
- Oxigenación.
- Iluminación.
- Ventilación/intracción/extracción.
- Buzzer/alertas.

## Estructura del proyecto

```text
src/
├── main.cpp                    # Entry point + tareas FreeRTOS
├── hal_setup.cpp/.h            # TFT + Touch + LVGL init
├── data_model.cpp/.h           # Modelo compartido thread-safe
├── assets_fs.cpp/.h            # LittleFS + PNG decoder
├── lv_conf.h                   # Config LVGL
├── generated_assets.cpp        # Assets embebidos
├── automation/
│   ├── automation_engine.h
│   └── automation_engine.cpp   # Motor de automatización
├── comms/
│   ├── uart_comm.h
│   └── uart_comm.cpp           # Comunicación UART con UNO R4
├── network/
│   ├── network_manager.h
│   └── network_manager.cpp     # WiFi + HTTP REST API
├── storage/
│   ├── settings_store.h
│   ├── settings_store.cpp
│   ├── storage_manager.h
│   └── storage_manager.cpp     # Configuración + data logger
└── ui/
    ├── ui_manager.h
    ├── ui_manager.cpp          # Pantallas LVGL
    ├── themes.h
    └── themes.cpp

include/
├── app_assets.h
└── generated_assets.h

controller_uno/
├── platformio.ini              # Target oficial: Arduino UNO R4 Minima
└── src/main.cpp                # Firmware del controlador I/O
```

## Pantallas de la interfaz

| # | Pantalla | Descripción |
|---|----------|-------------|
| 0 | Boot Test | Pantalla de diagnóstico |
| 1 | Splash | Animación de arranque |
| 2 | Dashboard | Vista rápida de sensores y estado |
| 3 | Menú Principal | Navegación general |
| 4 | Sensores | Lectura detallada |
| 5 | Etapas | Perfiles configurables |
| 6 | Actuadores | Control manual/auto |
| 7 | Alertas | Centro de alarmas |
| 8 | Calibración | Ajustes de sensores |
| 9 | Ajustes | Tema, UART y configuración |
| 10 | WiFi/API | Estado de red y endpoints |

## Tareas FreeRTOS del ESP32

| Tarea | Core | Función |
|-------|------|---------|
| UI | 1 | LVGL rendering + actualización de pantallas |
| UART | 0 | Comunicación bidireccional con UNO R4 |
| Automation | 1 | Evaluación de reglas automáticas |
| Network | 0 | WiFi + servidor HTTP REST |
| Storage | 1 | Registro histórico y persistencia |

## API REST local

### Modo WiFi por defecto

| Parámetro | Valor |
|-----------|-------|
| SSID | `e-Weed-AP` |
| Password | `eweed1234` |
| IP | `192.168.4.1` |

Si se configura una red WiFi externa, el ESP32 puede funcionar en modo estación con fallback a Access Point.

### Endpoints principales

#### `GET /api/status`

Devuelve el estado completo del sistema.

#### `GET /api/sensors`

Devuelve solo las lecturas de sensores.

#### `GET /api/config`

Devuelve configuración activa.

#### `POST /api/config`

Actualiza parámetros de configuración.

#### `POST /api/actuator`

Permite accionar salidas en modo autorizado.

## Compilación

### ESP32 HMI

Desde la raíz del proyecto:

```bash
pio run
pio run -t upload
pio device monitor
```

### Arduino UNO R4 Minima

Desde la carpeta del controlador:

```bash
cd controller_uno
pio run -e uno_r4_minima
pio run -e uno_r4_minima -t upload
pio device monitor -e uno_r4_minima
```

## Configuración PlatformIO

El proyecto principal usa:

- `platform = espressif32`
- `board = esp32dev`
- `framework = arduino`
- `board_build.filesystem = littlefs`
- `lvgl/lvgl`
- `TFT_eSPI`
- `XPT2046_Touchscreen`

El controlador I/O usa:

- `platform = renesas-ra`
- `board = uno_r4_minima`
- `framework = arduino`

## Seguridad eléctrica

Este proyecto puede controlar cargas externas mediante relés. Las cargas de red eléctrica, como iluminación, intractores o extractores de 220V, deben aislarse completamente de la electrónica de bajo voltaje.

Recomendaciones mínimas:

- Separar físicamente 220V de 5V/3.3V/12V.
- Usar módulos de relé/SSR adecuados para la carga real.
- Añadir fusibles, borneras seguras y gabinete aislado.
- No manipular red eléctrica energizada.
- Validar el cableado con una persona calificada antes de uso continuo.

## Estado actual

Repositorio inicial funcional para desarrollo de firmware ESP32 + Arduino UNO R4 Minima. La base incluye HMI, comunicación UART, automatización, almacenamiento, WiFi/API y controlador I/O separado.

## Licencia

Proyecto privado/personal en desarrollo. Definir licencia antes de distribución pública o uso comercial.
