# AGENTS — Proyecto e-Weed (`c:\e-Weed`)

## Qué es

Firmware **dual**: `controller_uno/` (Arduino UNO R4 o **Mega 2560** via PlatformIO) + raíz del repo (**ESP32** LVGL HMI). El **Mega es la fuente de verdad** para relés y modo AUTO (pH, EC con A→B secuencial, dilución).

## Rutas clave

| Área | Ruta |
|------|------|
| Controlador | `controller_uno/src/main.cpp` + módulos (`uart_proto`, `automation`, `sensors`, `actuators`, …), `controller_uno/platformio.ini` (`uno_r4_minima`, `mega2560`) |
| HMI | `src/main.cpp`, `src/ui/ui_manager.cpp`, `src/ui/ui_render.cpp`, `src/comms/uart_comm.cpp`, `src/automation/automation_engine.cpp`, `src/storage/settings_store.cpp` |
| Perfiles de etapa (rangos) | `src/data_model.cpp` |
| Documentación de sistema | `docs/E-WEED-SYSTEM.md` |

## Reglas para cambios

1. **No duplicar automatismo**: si el Mega está en `AUTO` (`STS:AUTO=1`), el **ESP no debe** enviar `CMD:OUT:*` (ver `automation_engine.cpp`).
2. **Modo AUTO en Mega** exige RTC; errores deben ser `ERR:` visibles en el HMI (`AUTO_NEEDS_RTC`).
3. **Nutriente A y B** nunca simultáneos en el mismo instante (interlock); secuencia 1:1 en AUTO y en `DOSE_AB`.
4. Tras editar `main.cpp` del controlador, sincronizar si usas Arduino IDE: `controller_uno_cli/eweed_uno/eweed_uno.ino` (copia manual).

## Build

```text
cd controller_uno && pio run -e mega2560
cd c:\e-Weed && pio run -e esp32dev
```

## Otros productos del monorepo

`xiw/`, `zea/`, `xyt/` son proyectos distintos; no mezclar prompts de otras apps con e-Weed.
