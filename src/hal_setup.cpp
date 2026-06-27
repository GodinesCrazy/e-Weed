#include "hal_setup.h"
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>

// Instancias globales
TFT_eSPI tft = TFT_eSPI(); 

// En la CYD la TFT queda en VSPI por defecto con TFT_eSPI, asi que el touch
// debe ir en HSPI para no reconfigurar el mismo bus y corromper la imagen.
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

SPIClass mySpi(HSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

static constexpr int TOUCH_MIN_X = 200;
static constexpr int TOUCH_MAX_X = 3700;
static constexpr int TOUCH_MIN_Y = 240;
static constexpr int TOUCH_MAX_Y = 3800;
static constexpr bool TOUCH_MIRROR_X = false;
static constexpr bool TOUCH_MIRROR_Y = false;

static uint32_t s_touch_log_count = 0;
static uint32_t s_last_touch_log_ms = 0;

static int clamp_coord(int value, int min_value, int max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

/* Buffer principal para fluidez de LVGL */
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[SCREEN_WIDTH * 20];

/* Funciones callback obligatorias para LVGL v8 */
void my_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1);
    tft.pushColors(reinterpret_cast<uint16_t *>(&color_p->full), (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1), true);
    tft.endWrite();

    lv_disp_flush_ready(disp_drv);
}

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    (void)indev_driver;

    if (ts.touched()) {
        TS_Point p = ts.getPoint();

        // El touch se alinea con la rotacion landscape normal del TFT.
        int x = map(p.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, SCREEN_WIDTH - 1);
        int y = map(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, SCREEN_HEIGHT - 1);
        if (TOUCH_MIRROR_X) {
            x = SCREEN_WIDTH - 1 - x;
        }
        if (TOUCH_MIRROR_Y) {
            y = SCREEN_HEIGHT - 1 - y;
        }
        x = clamp_coord(x, 0, SCREEN_WIDTH - 1);
        y = clamp_coord(y, 0, SCREEN_HEIGHT - 1);

        data->state = LV_INDEV_STATE_PR;
        data->point.x = x;
        data->point.y = y;

        const uint32_t now = millis();
        if (s_touch_log_count < 8 && now - s_last_touch_log_ms > 180) {
            s_last_touch_log_ms = now;
            ++s_touch_log_count;
            Serial.printf("TOUCH[%lu]: raw=(%d,%d,%d) map=(%d,%d)\n",
                          static_cast<unsigned long>(s_touch_log_count),
                          p.x, p.y, p.z, x, y);
        }
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

void hal_log_touch_config() {
    Serial.printf(
        "Touch cfg: rot=%d irq=%d cs=%d clk=%d miso=%d mosi=%d rawX=%d..%d rawY=%d..%d mirrorX=%d mirrorY=%d\n",
        SCREEN_ROTATION,
        XPT2046_IRQ,
        XPT2046_CS,
        XPT2046_CLK,
        XPT2046_MISO,
        XPT2046_MOSI,
        TOUCH_MIN_X,
        TOUCH_MAX_X,
        TOUCH_MIN_Y,
        TOUCH_MAX_Y,
        TOUCH_MIRROR_X ? 1 : 0,
        TOUCH_MIRROR_Y ? 1 : 0);
}

void hal_setup() {
    Serial.println("Initialzing TFT...");
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
    tft.begin();
    tft.setRotation(SCREEN_ROTATION);
    delay(50);
    tft.fillScreen(TFT_BLACK);

    Serial.println("Initializing Touch...");
    mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(mySpi);
    ts.setRotation(1);
    hal_log_touch_config();

    Serial.println("Initializing LVGL...");
    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, SCREEN_WIDTH * 20);

    // Registro Display en LVGL
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_WIDTH;
    disp_drv.ver_res = SCREEN_HEIGHT;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // Registro Input (Touch) en LVGL
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);
}

void hal_loop() {
    lv_tick_inc(5);
    lv_timer_handler();
    delay(5); // Pequeño delay de gracia (LVGL)
}
