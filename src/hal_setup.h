#ifndef HAL_SETUP_H
#define HAL_SETUP_H

#include <Arduino.h>

// Definiciones generales para el Touch del CYD (Cheap Yellow Display)
// La UI del proyecto vive en landscape 320x240 y la orientacion se fija
// directamente en el hardware del panel/touch.
// Rotation 1 en ILI9341_2_DRIVER deja el panel en horizontal sin espejo.
#define SCREEN_ROTATION 1
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// Declaración de inicio global del Hardware (TFT + Touch + LVGL buffer)
void hal_setup();
void hal_log_touch_config();

// Alimentador periódico para lv_tick de LVGL (satisface sus llamadas asíncronas)
void hal_loop();

#endif // HAL_SETUP_H
