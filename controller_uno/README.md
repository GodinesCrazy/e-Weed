# e-Weed UNO R4 Controller

Subproyecto para el controlador principal del sistema sobre `Arduino UNO R4 Minima`.

## Responsabilidades
- Leer `DS3231` por `I2C`.
- Leer sensores de nivel.
- Ejecutar la logica principal de control.
- Accionar relays.
- Publicar estado hacia el HMI `ESP32-2432S028` por `Serial1`.
- Recibir comandos `CMD:*` desde el HMI.

## Compilacion
Este subproyecto esta separado del HMI para no mezclar configuraciones de `ESP32` y `UNO R4`.

Ruta:
- [controller_uno/platformio.ini](C:\e-Weed\controller_uno\platformio.ini)

Comandos esperados:
```powershell
cd C:\e-Weed\controller_uno
platformio run
platformio run --target upload
```

## UART hacia HMI
- `Serial`: depuracion por USB.
- `Serial1`: enlace con el ESP32 HMI en pines `D0/D1`.

## Ajustes iniciales importantes
Antes de llevarlo a hardware real, revisa en [controller_uno/src/main.cpp](C:\e-Weed\controller_uno\src\main.cpp):
- pines de relays
- polaridad activa del modulo de relays
- polaridad de los sensores de nivel
- si quieres habilitar `kEnableDemoTelemetry`

