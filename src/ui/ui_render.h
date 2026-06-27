#ifndef EWEED_UI_RENDER_H
#define EWEED_UI_RENDER_H

#include "ui/ui_manager.h"
#include <lvgl.h>

void ui_render_init();
void ui_render_rebuild_theme();
void ui_render_tick(uint32_t now_ms);
void ui_render_ensure(ScreenId id);
void ui_render_destroy_all();
void ui_render_bump_activity();
lv_obj_t *ui_render_screen_ptr(ScreenId id);

#endif
