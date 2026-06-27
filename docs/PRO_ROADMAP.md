# e-Weed — Roadmap para dejar el proyecto nivel producto

## Estado actual

El proyecto ya tiene una base sólida:

- ESP32 HMI con LVGL.
- Arduino UNO R4 Minima como controlador I/O oficial.
- UART HMI ↔ controlador.
- API REST local.
- Pantalla touch rediseñada.
- Calibración guiada inicial.
- Interlocks críticos reforzados.
- CI para compilar ambos firmwares.
- Documentación de arquitectura, hardware y QA.

## Prioridad 1 — Validación física

Antes de seguir agregando funciones:

1. Compilar ESP32.
2. Compilar UNO R4.
3. Cargar ambos firmwares.
4. Validar UART estable.
5. Validar mapa de pines UNO R4.
6. Probar relés sin carga real.
7. Probar sensores en modo manual.
8. Probar interlocks con flotadores.

## Prioridad 2 — Calibración avanzada

La calibración debería evolucionar a:

- Captura real de punto pH 4.00.
- Captura real de punto pH 7.00.
- Cálculo automático de pendiente/offset.
- Detección de lectura estable antes de guardar.
- Historial de calibración.
- Validación de rango plausible.
- Mensajes de error si la sonda está desconectada o fuera de rango.

## Prioridad 3 — UX premium

Mejoras recomendadas:

- Iconos simples en dashboard.
- Estados visuales verde/amarillo/rojo por tarjeta.
- Confirmación para cargas 220V.
- Pantalla de diagnóstico UART.
- Pantalla de historial de alarmas más detallada.
- Modo básico/avanzado para ajustes.

## Prioridad 4 — API y operación local

- Documentar endpoints reales.
- Agregar endpoint de salud.
- Agregar endpoint de calibración.
- Exportar histórico CSV.
- Permitir backup/restore de configuración.

## Prioridad 5 — Robustez

- Reportar errores de salida bloqueada desde UNO R4 hacia HMI.
- Agregar watchdog lógico de UART.
- Agregar prueba automática de relés en mantenimiento.
- Agregar modo simulación documentado.
- Agregar perfiles por cultivo/configuración sin tocar código.

## Criterio de proyecto profesional

El proyecto estará en nivel producto cuando:

- Compile automáticamente en GitHub Actions.
- Tenga documentación suficiente para cablear y probar.
- La UI sea clara para usuario no técnico.
- El sistema arranque siempre en estado seguro.
- El UNO R4 pueda bloquear salidas aunque la HMI mande comandos incorrectos.
- La calibración sea guiada y verificable.
- Las cargas 220V estén tratadas como zona de riesgo.
