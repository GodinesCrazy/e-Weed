# Auditoria Grafica HMI (ESP32 320x240)

Fecha: 2026-04-03  
Objetivo: evaluar legibilidad, UX, consistencia visual y costo tecnico de render en HMI LVGL.

---

## 1) Problemas detectados

### 1.1 Legibilidad y jerarquia

1. En tarjetas compactas (sensores/actuadores) el texto compite con icono + estado en poco alto útil.
2. El footer en pantallas de datos concentra demasiada información en una sola línea.
3. Etiquetas de actuadores mantienen mezcla parcial de español/abreviaturas (`Recirc`, `Vent In`, `Vent Out`), bajando claridad.
4. Estado global principal (`AUTO | ETAPA | UART`) es correcto, pero sin separación visual por chips; en ambientes de brillo puede perderse.

### 1.2 Consistencia visual

1. Estilo general está bien alineado (glow azul, paneles glass), pero hay diferencias de densidad de contenido entre pantallas.
2. Algunas cadenas largas (alarma/acción) quedan truncadas en modo `DOT`.
3. En fondo brillante + texto blanco hay zonas de contraste medio (depende del asset de fondo).

### 1.3 Navegación y feedback

1. Navegación superior es consistente y usable.
2. Feedback de botón existe (pressed/checked), bien implementado.
3. Falta indicador visual más explícito de “comando enviado / comando confirmado”.

### 1.4 Costo técnico / rendimiento

1. PNG runtime en assets grandes falla por RAM, mitigado por fallback embebido.
2. Fondos + sombras + zoom alto elevan costo de draw en 320x240.
3. Actualización periódica a 300 ms funciona, pero puede optimizarse evitando refresco de labels sin cambios.

---

## 2) Mejora recomendada (sin romper arquitectura)

## Capa A: Visual / UX

1. Unificar 100% español en etiquetas de actuadores:
   - `Recirculacion`, `Vent. Entrada`, `Vent. Salida`.
2. Separar estado superior en badges/chips:
   - `MODO`, `ETAPA`, `UART`, `ALERTA`.
3. Reducir densidad en footer:
   - línea 1: estado enlace/telemetría
   - línea 2: última acción breve.
4. Priorizar tipografía de valores (sensor principal) sobre etiquetas secundarias.
5. Estado de comando:
   - “ENVIADO” (inmediato) y “CONFIRMADO” (con ACK real).

## Capa B: Técnica (render/memoria)

1. Mantener fallback embebido como camino principal estable.
2. Disminuir sombras y opacidades en paneles de alta frecuencia para reducir fill cost.
3. Evitar `lv_label_set_text` cuando el valor no cambió (dirty update).
4. Consolidar conversiones/formateo para minimizar alocaciones temporales de strings.
5. Reservar PNG runtime solo para diagnóstico o assets pequeños.

---

## 3) Mejoras ya implementadas (estado actual)

1. UI en español base + etapas solicitadas.
2. Corrección de crash por `lv_label_set_text_fmt` con float.
3. Botones con realce visual (pressed/checked + transición).
4. Ajuste previo de posiciones para reducir recorte inferior.
5. Fallback embebido activo para evitar pantalla vacía por OOM de PNG.

---

## 4) Mejoras descartadas por ahora (y por qué)

1. Rehacer visual completo de todas pantallas:
   - Descartado en esta fase para no romper navegación estable existente.
2. Eliminar fondos gráficos:
   - Descartado por pérdida de identidad visual del proyecto.
3. Forzar decode PNG runtime como camino único:
   - Descartado por riesgo de RAM/OOM en hardware real.

---

## 5) Impacto visual esperado al aplicar recomendaciones

1. Lectura más rápida de estado crítico (modo/enlace/alerta).
2. Menor confusión en actuadores por nomenclatura unificada.
3. Menos saturación textual en pie de pantalla.
4. Mejor trazabilidad para usuario final (“accion enviada vs confirmada”).

---

## 6) Impacto técnico esperado

1. Menor carga de render por reducción de efectos no esenciales.
2. Menos churn de memoria por updates de texto condicionados.
3. Mayor robustez al mantener fallback embebido como base.
4. Mejor depuración de UART al enlazar feedback visual con ACK/NACK real.

---

## 7) Conclusión de auditoria grafica

Estado actual HMI:
- **Usable y funcional**, con estilo consistente.
- Aún requiere ajustes de claridad fina y telemetría confirmada para experiencia final “producto”.

Prioridad para siguiente fase:
1. Completar enlace UART real.
2. Aplicar mejoras UX de estado confirmado sin romper layout estable.

