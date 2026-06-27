#include "ui/ui_manager.h"
#include "ui/ui_render.h"
#include "data_model.h"
#include "storage/settings_store.h"

void UiManager::init() {
    DataModel::getInstance().lock();
    settingsLoadInto(DataModel::getInstance().getSettings());
    DataModel::getInstance().unlock();
    ui_render_init();
    ui_render_bump_activity();
    loadScreen(SCR_SPLASH);
}

void UiManager::loadScreen(ScreenId id) {
    if (id >= SCR_COUNT) {
        return;
    }
    ui_render_ensure(id);
    lv_obj_t *sc = ui_render_screen_ptr(id);
    if (!sc) {
        return;
    }
    currentScreen = id;
    ui_render_bump_activity();
    lv_scr_load_anim(sc, LV_SCR_LOAD_ANIM_FADE_ON, 120, 0, false);
}

void UiManager::update() { ui_render_tick(millis()); }

void UiManager::bumpActivity() { ui_render_bump_activity(); }
