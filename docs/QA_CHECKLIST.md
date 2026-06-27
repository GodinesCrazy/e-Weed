# e-Weed — Checklist QA de puesta en marcha

Usar este checklist después de cada cambio importante de firmware, UI o hardware.

## 1. Compilación

Desde la raíz del proyecto:

```bash
pio run -e esp32dev
```

Desde el controlador:

```bash
cd controller_uno
pio run -e uno_r4_minima
```

Criterio de aceptación:

- Ambos builds terminan en `SUCCESS`.
- No hay errores de link.
- No hay librerías faltantes.

## 2. Arranque seguro UNO R4

Al energizar:

- Todos los relés deben iniciar apagados.
- El modo inicial debe ser mantenimiento seguro.
- La HMI debe recibir `ACK:BOOT` y luego `STS`.
- No debe activarse ninguna bomba al iniciar.

## 3. Enlace UART

Verificar:

- ESP32 muestra `UART OK`.
- UNO R4 responde a `CMD:GET_STATUS`.
- La HMI actualiza pH, TDS, temperatura, humedad y relés.
- Si se desconecta UART, la HMI debe indicar pérdida de enlace.

## 4. Sensores

Verificar en pantalla Sensores:

- pH válido o marcado como inválido.
- TDS/EC válido o marcado como inválido.
- Temperatura de agua válida.
- DHT válido o `OFF`.
- Nivel mínimo y máximo cambian al accionar flotadores.
- RTC disponible o indicado como no OK.

## 5. Interlocks críticos

Probar con cuidado y sin químicos reales durante la primera validación.

| Prueba | Resultado esperado |
|---|---|
| Activar PA y luego PB | PA debe apagarse o PB debe quedar bloqueado |
| Activar PHU y luego PHD | PHU debe apagarse o PHD debe quedar bloqueado |
| Nivel mínimo activo + dosificación | Bombas críticas bloqueadas |
| Nivel máximo activo + llenado | PIN bloqueado |
| AUTO activo + switch manual | HMI no debe permitir conmutación |

## 6. UI touch

Verificar en pantalla:

- Splash legible.
- Botón de entrada responde.
- Dashboard con tarjetas no se corta.
- Menú principal muestra botones grandes.
- Calibración abre sus pestañas.
- Actuadores separa 12V y 220V.
- No aparece texto visible `Mega`.

## 7. Calibración pH

Flujo mínimo:

1. Entrar a modo mantenimiento.
2. Lavar sonda.
3. Insertar en solución pH 4.00.
4. Esperar estabilidad.
5. Guardar/calcular punto.
6. Repetir con solución pH 7.00.
7. Confirmar que offset y pendiente guardan.
8. Reiniciar y verificar persistencia.

## 8. Calibración TDS/EC

1. Usar solución patrón conocida.
2. Esperar estabilidad térmica.
3. Ajustar factor solo si la lectura estable difiere del patrón.
4. Guardar.
5. Reiniciar y verificar persistencia.

## 9. Seguridad 220V

Antes de conectar luz/intractor/extractor reales:

- Validar relés sin carga de red.
- Confirmar aislamiento físico.
- Confirmar fusibles/protecciones.
- Confirmar caja cerrada.
- Validar con una persona calificada.

## 10. Criterio antes de usar en continuo

El sistema solo debería operar en continuo cuando:

- Ambos firmwares compilan.
- UART se mantiene estable por al menos 30 minutos.
- Sensores entregan valores razonables.
- Interlocks fueron probados.
- El modo AUTO no arranca sin validaciones.
- La calibración fue documentada.
