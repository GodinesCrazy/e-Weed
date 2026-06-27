#include "ui/ui_render.h"
#include "app_assets.h"
#include "assets_fs.h"
#include "comms/uart_comm.h"
#include "data_model.h"
#include "eweed/fw_version.h"
#include "generated_assets.h"
#include "storage/settings_store.h"

#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

static lv_obj_t *g_scr[SCR_COUNT] = {};
static uint32_t  g_activity_ms    = 0;

namespace {

const char *fmt_ph(const SystemState &st);
const char *fmt_ec(const SystemState &st);

const lv_font_t *F_S = &lv_font_montserrat_12;
const lv_font_t *F_M = &lv_font_montserrat_16;
const lv_font_t *F_L = &lv_font_montserrat_20;

lv_style_t sty_bg, sty_card, sty_btn, sty_btn_pri, sty_title;
lv_color_t C_BG, C_CARD, C_TXT, C_MUT, C_ACC, C_OK, C_WRN, C_ERR, C_BR;

bool     s_dark            = true;
bool     s_theme_rebuild   = false;
ScreenId s_theme_resume    = SCR_HOME;
uint32_t s_tick_prev       = 0;
uint32_t s_splash_ms       = 0;
char     s_act_snap[64]    = "";
uint32_t s_act_snap_ms     = 0;

const char *kMegaStateEs[] = {
    "Arranque",          "Reposo",           "Leyendo sensores", "Validando sensores",
    "Comprobando nivel", "Llenado de agua",  "Evaluando pH",     "Dosificacion pH+",
    "Dosificacion pH-",  "Recirc. post pH",  "Evaluando TDS",    "Dosificacion A",
    "Recirc. post A",    "Dosificacion B",   "Recirc. post B",   "Estabilizacion",
    "Re-medicion",       "Control luz",      "Control vent.",    "Enviando estado",
    "ALARMA",            "Mantenimiento"};

const char *megaStateStr(uint8_t c) {
    if (c >= sizeof(kMegaStateEs) / sizeof(kMegaStateEs[0])) {
        return "FSM no informada";
    }
    return kMegaStateEs[c];
}

void palette(bool dark) {
    s_dark = dark;
    if (dark) {
        C_BG = lv_color_hex(0x0d1117);
        C_CARD = lv_color_hex(0x161b22);
        C_TXT = lv_color_hex(0xe6edf3);
        C_MUT = lv_color_hex(0x7d8590);
        C_ACC = lv_color_hex(0x58a6ff);
        C_OK = lv_color_hex(0x3fb950);
        C_WRN = lv_color_hex(0xd29922);
        C_ERR = lv_color_hex(0xf85149);
        C_BR = lv_color_hex(0x30363d);
    } else {
        C_BG = lv_color_hex(0xf0f2f5);
        C_CARD = lv_color_hex(0xffffff);
        C_TXT = lv_color_hex(0x1f2328);
        C_MUT = lv_color_hex(0x656d76);
        C_ACC = lv_color_hex(0x0969da);
        C_OK = lv_color_hex(0x1a7f37);
        C_WRN = lv_color_hex(0x9a6700);
        C_ERR = lv_color_hex(0xcf222e);
        C_BR = lv_color_hex(0xd0d7de);
    }
}

void styles_init() {
    palette(s_dark);
    lv_style_init(&sty_bg);
    lv_style_set_bg_color(&sty_bg, C_BG);
    lv_style_set_bg_opa(&sty_bg, LV_OPA_COVER);
    lv_style_set_text_color(&sty_bg, C_TXT);
    lv_style_set_text_font(&sty_bg, F_S);

    lv_style_init(&sty_card);
    lv_style_set_bg_color(&sty_card, C_CARD);
    lv_style_set_bg_opa(&sty_card, LV_OPA_COVER);
    lv_style_set_radius(&sty_card, 8);
    lv_style_set_border_color(&sty_card, C_BR);
    lv_style_set_border_width(&sty_card, 1);
    lv_style_set_pad_all(&sty_card, 6);

    lv_style_init(&sty_btn);
    lv_style_set_bg_color(&sty_btn, C_CARD);
    lv_style_set_bg_opa(&sty_btn, LV_OPA_COVER);
    lv_style_set_radius(&sty_btn, 8);
    lv_style_set_border_color(&sty_btn, C_BR);
    lv_style_set_border_width(&sty_btn, 1);
    lv_style_set_pad_all(&sty_btn, 4);

    lv_style_init(&sty_btn_pri);
    lv_style_set_bg_color(&sty_btn_pri, C_ACC);
    lv_style_set_bg_opa(&sty_btn_pri, LV_OPA_COVER);
    lv_style_set_radius(&sty_btn_pri, 8);
    lv_style_set_text_color(&sty_btn_pri, s_dark ? lv_color_hex(0x0d1117) : lv_color_white());

    lv_style_init(&sty_title);
    lv_style_set_text_font(&sty_title, F_M);
    lv_style_set_text_color(&sty_title, C_TXT);
}

void lblf(lv_obj_t *o, const char *fmt, ...) {
    if (!o || !fmt) {
        return;
    }
    char b[160];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof(b), fmt, a);
    va_end(a);
    lv_label_set_text(o, b);
}

lv_obj_t *mk_card(lv_obj_t *p, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    lv_obj_t *c = lv_obj_create(p);
    lv_obj_remove_style_all(c);
    lv_obj_add_style(c, &sty_card, 0);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

lv_obj_t *mk_btn(lv_obj_t *p, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, bool pri) {
    lv_obj_t *b = lv_btn_create(p);
    lv_obj_remove_style_all(b);
    lv_obj_add_style(b, &sty_btn, 0);
    if (pri) {
        lv_obj_add_style(b, &sty_btn_pri, 0);
    }
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    return b;
}

void hdr(lv_obj_t *scr, const char *title, ScreenId back) {
    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, title);
    lv_obj_add_style(t, &sty_title, 0);
    lv_obj_set_pos(t, 8, 4);
    if (back < SCR_COUNT) {
        lv_obj_t *bb = mk_btn(scr, 200, 2, 52, 22, false);
        lv_obj_t *lb = lv_label_create(bb);
        lv_label_set_text(lb, LV_SYMBOL_LEFT);
        lv_obj_center(lb);
        lv_obj_add_event_cb(
            bb,
            [](lv_event_t *e) {
                ScreenId id = static_cast<ScreenId>(
                    reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
                UiManager::getInstance().loadScreen(id);
            },
            LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(back)));
        lv_obj_t *bh = mk_btn(scr, 258, 2, 54, 22, true);
        lv_obj_t *lh = lv_label_create(bh);
        lv_label_set_text(lh, LV_SYMBOL_HOME);
        lv_obj_center(lh);
        lv_obj_add_event_cb(
            bh,
            [](lv_event_t *e) {
                (void)e;
                UiManager::getInstance().loadScreen(SCR_HOME);
            },
            LV_EVENT_CLICKED, nullptr);
    }
    lv_obj_t *ln = lv_obj_create(scr);
    lv_obj_remove_style_all(ln);
    lv_obj_set_size(ln, 320, 2);
    lv_obj_set_pos(ln, 0, 26);
    lv_obj_set_style_bg_color(ln, C_ACC, 0);
    lv_obj_set_style_bg_opa(ln, LV_OPA_40, 0);
}

void on_act(lv_event_t *e) {
    (void)e;
    ui_render_bump_activity();
}

void nav_tile(lv_obj_t *p, lv_coord_t x, lv_coord_t y, const char *tx, ScreenId to) {
    lv_obj_t *b = mk_btn(p, x, y, 98, 44, false);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, tx);
    lv_obj_set_style_text_font(l, F_S, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, 92);
    lv_obj_center(l);
    lv_obj_add_event_cb(
        b,
        [](lv_event_t *e) {
            ScreenId t = static_cast<ScreenId>(
                reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
            ui_render_bump_activity();
            UiManager::getInstance().loadScreen(t);
        },
        LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(to)));
}

void send_cmd(const char *c) { UartComm::getInstance().sendCommand(c); }

constexpr const char *kActKey[] = {"PA", "PB", "PHU", "PHD", "REC", "PIN",
                                   "LUZ", "INT", "EXT", "BUZ"};
constexpr const char *kActNm[] = {"Nutr. A", "Nutr. B", "pH+", "pH-", "Recirc.",
                                  "Agua IN", "Luz", "Int", "Ext", "Buzzer"};
bool s_act_sync = false;

void act_sw(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || s_act_sync) {
        return;
    }
    int i = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    DataModel::getInstance().lock();
    SystemState st = DataModel::getInstance().getState();
    DataModel::getInstance().unlock();
    if (st.auto_mode && !st.maintenance_mode) {
        return;
    }
    char b[40];
    snprintf(b, sizeof(b), "CMD:OUT:%s:%d", kActKey[i], on ? 1 : 0);
    send_cmd(b);
}

void mode_go(int m) {
    if (m == 0) {
        send_cmd("CMD:SET_MAINT:0");
        send_cmd("CMD:SET_AUTO:1");
    } else if (m == 1) {
        send_cmd("CMD:SET_AUTO:0");
        send_cmd("CMD:SET_MAINT:0");
    } else {
        send_cmd("CMD:SET_AUTO:0");
        send_cmd("CMD:SET_MAINT:1");
    }
}

struct WDash {
    lv_obj_t *st, *md, *ph, *ec, *tw, *ta, *ha, *lv, *al, *ac, *qk;
} wd;
struct WSens {
    lv_obj_t *ln[9];
} ws;
struct WAuto {
    lv_obj_t *md, *fsm, *ac, *tel, *co, *blk;
} wa;
struct WAct {
    lv_obj_t *sw[10], *wrn;
} wact;
struct WAlm {
    lv_obj_t *st, *hi;
} wal;
struct WCal {
    lv_obj_t *g;  // guide
    lv_obj_t *sv;
} wcal;
static lv_obj_t *g_sl_coff   = nullptr;
static lv_obj_t *g_sl_cslope = nullptr;
static lv_obj_t *g_sl_tdsf   = nullptr;
struct WMaint {
    lv_obj_t *u;
} wm;
struct WComm {
    lv_obj_t *wf = nullptr;
} wcomm;
struct WInf {
    lv_obj_t *t;
} wi;

static lv_obj_t *g_stg_txt = nullptr;

void refresh_stage_card() {
    if (!g_stg_txt) {
        return;
    }
    DataModel::getInstance().lock();
    SystemState st = DataModel::getInstance().getState();
    DataModel::getInstance().unlock();
    int i = clamp_stage(st.active_stage);
    const StageProfile &p = kStageProfiles[i];
    lblf(g_stg_txt,
         "%s\npH %.2f-%.2f  PPM %d-%d\nFoto %s\n"
         "Secuencia A->B 1:1; recirc. y estabilizacion en Mega.",
         p.name, p.ph_min, p.ph_max, p.ppm_min, p.ppm_max, p.photoperiod);
}

void build_splash() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_add_style(s, &sty_bg, 0);
    lv_obj_add_event_cb(s, on_act, LV_EVENT_PRESSED, nullptr);
    lv_obj_t *im = lv_img_create(s);
    if (assets_fs_file_exists(AppAssets::kSplashScreen)) {
        lv_img_set_src(im, AppAssets::kSplashScreen);
    } else {
        lv_img_set_src(im, &asset_logo_main_240x120);
    }
    lv_obj_align(im, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_t *a = lv_label_create(s);
    lv_label_set_text(a, "e-Weed");
    lv_obj_set_style_text_font(a, F_L, 0);
    lv_obj_set_style_text_color(a, C_ACC, 0);
    lv_obj_align(a, LV_ALIGN_TOP_MID, 0, 148);
    lv_obj_t *b = lv_label_create(s);
    lv_label_set_text(b, "Toque para continuar");
    lv_obj_set_style_text_color(b, C_MUT, 0);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(
        s,
        [](lv_event_t *e) {
            if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
                ui_render_bump_activity();
                UiManager::getInstance().loadScreen(SCR_HOME);
            }
        },
        LV_EVENT_CLICKED, nullptr);
    g_scr[SCR_SPLASH] = s;
}

void build_home() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_add_style(s, &sty_bg, 0);
    lv_obj_add_event_cb(s, on_act, LV_EVENT_PRESSED, nullptr);
    hdr(s, "Menu principal", SCR_COUNT);
    int y = 34;
    nav_tile(s, 6, y, "Dashboard", SCR_DASHBOARD);
    nav_tile(s, 110, y, "Etapas", SCR_STAGES);
    nav_tile(s, 214, y, "Sensores", SCR_SENSORS);
    y += 50;
    nav_tile(s, 6, y, "Actuadores", SCR_ACTUATORS);
    nav_tile(s, 110, y, "Automat.", SCR_AUTOMATION);
    nav_tile(s, 214, y, "Calibracion", SCR_CALIBRATION);
    y += 50;
    nav_tile(s, 6, y, "Alarmas", SCR_ALARMS);
    nav_tile(s, 110, y, "Configuracion", SCR_SETTINGS_HUB);
    nav_tile(s, 214, y, "Mantenimiento", SCR_MAINTENANCE);
    y += 50;
    nav_tile(s, 6, y, "Info sistema", SCR_SYSTEM_INFO);
    g_scr[SCR_HOME] = s;
}

void build_dashboard() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_add_style(s, &sty_bg, 0);
    lv_obj_add_event_cb(s, on_act, LV_EVENT_PRESSED, nullptr);
    hdr(s, "Dashboard", SCR_HOME);
    wd.st = lv_label_create(s);
    lv_obj_set_pos(wd.st, 8, 32);
    wd.md = lv_label_create(s);
    lv_obj_set_pos(wd.md, 8, 46);
    wd.ph = lv_label_create(s);
    lv_obj_set_pos(wd.ph, 8, 64);
    wd.ec = lv_label_create(s);
    lv_obj_set_pos(wd.ec, 8, 80);
    wd.tw = lv_label_create(s);
    lv_obj_set_pos(wd.tw, 8, 98);
    wd.ta = lv_label_create(s);
    lv_obj_set_pos(wd.ta, 160, 98);
    wd.ha = lv_label_create(s);
    lv_obj_set_pos(wd.ha, 8, 114);
    wd.lv = lv_label_create(s);
    lv_obj_set_pos(wd.lv, 160, 114);
    wd.al = lv_label_create(s);
    lv_obj_set_pos(wd.al, 8, 132);
    wd.ac = lv_label_create(s);
    lv_label_set_long_mode(wd.ac, LV_LABEL_LONG_DOT);
    lv_obj_set_width(wd.ac, 300);
    lv_obj_set_pos(wd.ac, 8, 150);
    wd.qk = lv_label_create(s);
    lv_obj_set_style_text_font(wd.qk, F_S, 0);
    lv_obj_set_pos(wd.qk, 8, 176);
    g_scr[SCR_DASHBOARD] = s;
}

void build_stages() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_add_style(s, &sty_bg, 0);
    lv_obj_add_event_cb(s, on_act, LV_EVENT_PRESSED, nullptr);
    hdr(s, "Etapas", SCR_HOME);
    for (int i = 0; i < 5; ++i) {
        lv_obj_t *b = mk_btn(s, 6, 32 + i * 28, 108, 24, false);
        lv_obj_t *l = lv_label_create(b);
        lblf(l, "Activar %d", i + 1);
        lv_obj_set_style_text_font(l, F_S, 0);
        lv_obj_center(l);
        lv_obj_add_event_cb(
            b,
            [](lv_event_t *e) {
                int ix = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
                char buf[28];
                snprintf(buf, sizeof(buf), "CMD:SET_STAGE:%d", ix);
                send_cmd(buf);
                ui_render_bump_activity();
                refresh_stage_card();
            },
            LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    }
    lv_obj_t *cd = mk_card(s, 122, 32, 190, 176);
    g_stg_txt = lv_label_create(cd);
    lv_label_set_long_mode(g_stg_txt, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_stg_txt, 178);
    lv_obj_set_pos(g_stg_txt, 4, 4);
    lv_obj_t *ok = lv_label_create(cd);
    lv_label_set_text(ok, LV_SYMBOL_OK " Confirmacion visual al recibir STS");
    lv_obj_set_style_text_color(ok, C_MUT, 0);
    lv_obj_set_pos(ok, 4, 150);
    refresh_stage_card();
    g_scr[SCR_STAGES] = s;
}

void build_sensors() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_add_style(s, &sty_bg, 0);
    lv_obj_add_event_cb(s, on_act, LV_EVENT_PRESSED, nullptr);
    hdr(s, "Sensores", SCR_HOME);
    int y = 32;
    for (int i = 0; i < 9; ++i) {
        ws.ln[i] = lv_label_create(s);
        lv_obj_set_pos(ws.ln[i], 6, y);
        y += 22;
    }
    g_scr[SCR_SENSORS] = s;
}

void build_actuators() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_add_style(s, &sty_bg, 0);
    lv_obj_add_event_cb(s, on_act, LV_EVENT_PRESSED, nullptr);
    hdr(s, "Actuadores", SCR_HOME);
    wact.wrn = lv_label_create(s);
    lv_obj_set_pos(wact.wrn, 6, 30);
    lv_obj_set_style_text_color(wact.wrn, C_WRN, 0);
    lv_label_set_text(wact.wrn, "AUTO: solo lectura. Use MANT o MANUAL.");
    for (int i = 0; i < 10; ++i) {
        lv_obj_t *row = mk_card(s, 6, 48 + i * 18, 308, 16);
        lv_obj_t *lb = lv_label_create(row);
        lblf(lb, "%s", kActNm[i]);
        lv_obj_set_pos(lb, 2, -2);
        wact.sw[i] = lv_switch_create(row);
        lv_obj_set_pos(wact.sw[i], 250, -4);
        lv_obj_set_size(wact.sw[i], 40, 18);
        lv_obj_add_event_cb(wact.sw[i], act_sw, LV_EVENT_VALUE_CHANGED,
                            reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    }
    g_scr[SCR_ACTUATORS] = s;
}

void build_automation() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_add_style(s, &sty_bg, 0);
    lv_obj_add_event_cb(s, on_act, LV_EVENT_PRESSED, nullptr);
    hdr(s, "Automatizacion", SCR_HOME);
    lv_obj_t *b0 = mk_btn(s, 6, 32, 96, 26, false);
    lv_obj_t *l0 = lv_label_create(b0);
    lv_label_set_text(l0, "Manual");
    lv_obj_center(l0);
    lv_obj_add_event_cb(b0, [](lv_event_t *e) {
        (void)e;
        mode_go(1);
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *b1 = mk_btn(s, 108, 32, 96, 26, true);
    lv_obj_t *l1 = lv_label_create(b1);
    lv_label_set_text(l1, "Auto");
    lv_obj_center(l1);
    lv_obj_add_event_cb(b1, [](lv_event_t *e) {
        (void)e;
        mode_go(0);
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *b2 = mk_btn(s, 210, 32, 100, 26, false);
    lv_obj_t *l2 = lv_label_create(b2);
    lv_label_set_text(l2, "Mantenim.");
    lv_obj_center(l2);
    lv_obj_add_event_cb(b2, [](lv_event_t *e) {
        (void)e;
        mode_go(2);
    }, LV_EVENT_CLICKED, nullptr);
    wa.md = lv_label_create(s);
    lv_obj_set_pos(wa.md, 6, 64);
    wa.fsm = lv_label_create(s);
    lv_obj_set_pos(wa.fsm, 6, 80);
    wa.ac = lv_label_create(s);
    lv_label_set_long_mode(wa.ac, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(wa.ac, 300);
    lv_obj_set_pos(wa.ac, 6, 98);
    wa.tel = lv_label_create(s);
    lv_obj_set_pos(wa.tel, 6, 140);
    wa.co = lv_label_create(s);
    lv_obj_set_pos(wa.co, 6, 158);
    wa.blk = lv_label_create(s);
    lv_label_set_long_mode(wa.blk, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(wa.blk, 300);
    lv_obj_set_pos(wa.blk, 6, 176);
    g_scr[SCR_AUTOMATION] = s;
}

void build_calibration() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_add_style(s, &sty_bg, 0);
    lv_obj_add_event_cb(s, on_act, LV_EVENT_PRESSED, nullptr);
    hdr(s, "Calibracion", SCR_HOME);
    lv_obj_t *tv = lv_tabview_create(s, LV_DIR_TOP, 24);
    lv_obj_set_size(tv, 320, 210);
    lv_obj_set_pos(tv, 0, 28);
    lv_obj_t *t1 = lv_tabview_add_tab(tv, "pH");
    lv_obj_t *t2 = lv_tabview_add_tab(tv, "TDS");
    lv_obj_t *t3 = lv_tabview_add_tab(tv, "Valores");
    lv_obj_t *t4 = lv_tabview_add_tab(tv, "Reset");
    lv_obj_t *t5 = lv_tabview_add_tab(tv, "Guia");

    lv_obj_t *bstart = mk_btn(t1, 8, 8, 120, 28, false);
    lv_obj_t *ls = lv_label_create(bstart);
    lv_label_set_text(ls, "Iniciar (Mega)");
    lv_obj_center(ls);
    lv_obj_add_event_cb(bstart, [](lv_event_t *e) {
        (void)e;
        send_cmd("CMD:CAL_PH_START");
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *lco = lv_label_create(t1);
    lv_label_set_text(lco, "Offset pH (slider/100)");
    lv_obj_set_pos(lco, 8, 42);
    g_sl_coff = lv_slider_create(t1);
    lv_slider_set_range(g_sl_coff, -3000, 3500);
    lv_obj_set_pos(g_sl_coff, 8, 56);
    lv_obj_set_size(g_sl_coff, 200, 10);
    lv_obj_t *lcs = lv_label_create(t1);
    lv_label_set_text(lcs, "Pendiente (slider/100)");
    lv_obj_set_pos(lcs, 8, 72);
    g_sl_cslope = lv_slider_create(t1);
    lv_slider_set_range(g_sl_cslope, -1000, 500);
    lv_obj_set_pos(g_sl_cslope, 8, 86);
    lv_obj_set_size(g_sl_cslope, 200, 10);

    lv_obj_t *bsend = mk_btn(t1, 140, 8, 160, 28, true);
    lv_obj_t *ld = lv_label_create(bsend);
    lv_label_set_text(ld, "Guardar offset/pend.");
    lv_obj_center(ld);
    lv_obj_add_event_cb(bsend, [](lv_event_t *e) {
        (void)e;
        DataModel::getInstance().lock();
        SystemSettings ss = DataModel::getInstance().getSettings();
        DataModel::getInstance().unlock();
        ss.ph_offset = static_cast<float>(lv_slider_get_value(g_sl_coff)) / 100.0f;
        ss.ph_slope  = static_cast<float>(lv_slider_get_value(g_sl_cslope)) / 100.0f;
        DataModel::getInstance().updateSettings(ss);
        char buf[48];
        snprintf(buf, sizeof(buf), "CMD:CAL_PH_SAVE:%.3f:%.3f", ss.ph_offset, ss.ph_slope);
        send_cmd(buf);
        settingsSaveFrom(ss);
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *btds = mk_btn(t2, 8, 8, 140, 28, true);
    lv_obj_t *lt = lv_label_create(btds);
    lv_label_set_text(lt, "Inicio TDS Mega");
    lv_obj_center(lt);
    lv_obj_add_event_cb(btds, [](lv_event_t *e) {
        (void)e;
        send_cmd("CMD:CAL_TDS_START");
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *ltf = lv_label_create(t2);
    lv_label_set_text(ltf, "Factor x0.01");
    lv_obj_set_pos(ltf, 8, 40);
    g_sl_tdsf = lv_slider_create(t2);
    lv_slider_set_range(g_sl_tdsf, 50, 400);
    lv_obj_set_pos(g_sl_tdsf, 8, 54);
    lv_obj_set_size(g_sl_tdsf, 280, 10);
    lv_obj_t *bts = mk_btn(t2, 150, 8, 150, 28, false);
    lv_obj_t *lt2 = lv_label_create(bts);
    lv_label_set_text(lt2, "Guardar factor");
    lv_obj_center(lt2);
    lv_obj_add_event_cb(bts, [](lv_event_t *e) {
        (void)e;
        float f = static_cast<float>(lv_slider_get_value(g_sl_tdsf)) / 100.0f;
        DataModel::getInstance().lock();
        SystemSettings ss = DataModel::getInstance().getSettings();
        DataModel::getInstance().unlock();
        ss.tds_cal_factor = f;
        DataModel::getInstance().updateSettings(ss);
        char buf[40];
        snprintf(buf, sizeof(buf), "CMD:CAL_TDS_SAVE:%.4f", f);
        send_cmd(buf);
        settingsSaveFrom(ss);
    }, LV_EVENT_CLICKED, nullptr);

    wcal.sv = lv_label_create(t3);
    lv_label_set_long_mode(wcal.sv, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(wcal.sv, 280);
    lv_obj_set_pos(wcal.sv, 8, 8);

    lv_obj_t *br = mk_btn(t4, 8, 8, 280, 32, true);
    lv_obj_t *lr = lv_label_create(br);
    lv_label_set_text(lr, "Restaurar fab. Mega (21.34/-5.70, 1.0)");
    lv_obj_center(lr);
    lv_obj_add_event_cb(br, [](lv_event_t *e) {
        (void)e;
        send_cmd("CMD:CAL_PH_SAVE:21.340:-5.700");
        send_cmd("CMD:CAL_TDS_SAVE:1.0000");
    }, LV_EVENT_CLICKED, nullptr);

    wcal.g = lv_label_create(t5);
    lv_label_set_long_mode(wcal.g, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(wcal.g, 280);
    lv_label_set_text(wcal.g,
                      "1) Modo mantenimiento.\n"
                      "2) pH: soluciones patron, siga rutina Mega.\n"
                      "3) Ajuste offset/pendiente en Valores (NVS) y pulse Guardar.\n"
                      "4) TDS: factor lineal; verificar con patron.\n"
                      "5) Nutriente A y B nunca simultaneos.");
    lv_obj_set_pos(wcal.g, 8, 8);

    g_scr[SCR_CALIBRATION] = s;
}

void build_alarms() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_add_style(s, &sty_bg, 0);
    lv_obj_add_event_cb(s, on_act, LV_EVENT_PRESSED, nullptr);
    hdr(s, "Alarmas", SCR_HOME);
    wal.st = lv_label_create(s);
    lv_obj_set_pos(wal.st, 8, 32);
    wal.hi = lv_label_create(s);
    lv_label_set_long_mode(wal.hi, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(wal.hi, 300);
    lv_obj_set_pos(wal.hi, 8, 56);
    lv_obj_t *ack = mk_btn(s, 8, 190, 140, 28, true);
    lv_obj_t *la = lv_label_create(ack);
    lv_label_set_text(la, "ACK / Reset");
    lv_obj_center(la);
    lv_obj_add_event_cb(ack, [](lv_event_t *e) {
        (void)e;
        send_cmd("CMD:ACK_ALARM");
    }, LV_EVENT_CLICKED, nullptr);
    g_scr[SCR_ALARMS] = s;
}

void hub_item(lv_obj_t *p, lv_coord_t y, const char *tx, ScreenId to) {
    lv_obj_t *b = mk_btn(p, 8, y, 300, 22, false);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, tx);
    lv_obj_set_style_text_font(l, F_S, 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_add_event_cb(
        b,
        [](lv_event_t *e) {
            ui_render_bump_activity();
            ScreenId t = static_cast<ScreenId>(
                reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
            UiManager::getInstance().loadScreen(t);
        },
        LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(to)));
}

void build_settings_hub() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_add_style(s, &sty_bg, 0);
    lv_obj_add_event_cb(s, on_act, LV_EVENT_PRESSED, nullptr);
    hdr(s, "Ajustes", SCR_HOME);
    int y = 32;
    hub_item(s, y, "Parametros por etapa (local)", SCR_SET_STAGE);
    y += 26;
    hub_item(s, y, "Limites de seguridad", SCR_SET_SAFETY);
    y += 26;
    hub_item(s, y, "Recirculacion (local)", SCR_SET_RECIRC);
    y += 26;
    hub_item(s, y, "Estabilizacion (local)", SCR_SET_STAB);
    y += 26;
    hub_item(s, y, "Microdosis (local)", SCR_SET_MICRO);
    y += 26;
    hub_item(s, y, "Fotoperiodo (local)", SCR_SET_PHOTO);
    y += 26;
    hub_item(s, y, "Ventilacion (local)", SCR_SET_VENT);
    y += 26;
    hub_item(s, y, "Comunicacion / WiFi", SCR_SET_COMM);
    y += 26;
    hub_item(s, y, "Pantalla", SCR_SET_DISPLAY);
    y += 26;
    hub_item(s, y, "Sonido", SCR_SET_SOUND);
    g_scr[SCR_SETTINGS_HUB] = s;
}

static lv_obj_t *g_roll_st = nullptr;
static lv_obj_t *g_sl_ph   = nullptr;
static lv_obj_t *g_sl_tds  = nullptr;

void build_set_stage_v2() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_add_style(s, &sty_bg, 0);
    lv_obj_add_event_cb(s, on_act, LV_EVENT_PRESSED, nullptr);
    hdr(s, "Objetivos por etapa", SCR_SETTINGS_HUB);
    g_roll_st = lv_roller_create(s);
    lv_roller_set_options(g_roll_st,
                          "1 Germinacion\n2 Vegetativo\n3 Transicion\n4 Produccion\n5 Maduracion",
                          LV_ROLLER_MODE_NORMAL);
    lv_obj_set_width(g_roll_st, 140);
    lv_obj_set_pos(g_roll_st, 8, 34);
    lv_obj_t *h1 = lv_label_create(s);
    lv_label_set_text(h1, "pH");
    lv_obj_set_pos(h1, 160, 36);
    g_sl_ph = lv_slider_create(s);
    lv_slider_set_range(g_sl_ph, 550, 750);
    lv_obj_set_size(g_sl_ph, 140, 12);
    lv_obj_set_pos(g_sl_ph, 160, 54);
    lv_obj_t *h2 = lv_label_create(s);
    lv_label_set_text(h2, "PPM");
    lv_obj_set_pos(h2, 160, 72);
    g_sl_tds = lv_slider_create(s);
    lv_slider_set_range(g_sl_tds, 50, 2500);
    lv_obj_set_size(g_sl_tds, 140, 12);
    lv_obj_set_pos(g_sl_tds, 160, 90);
    lv_obj_t *bs = mk_btn(s, 8, 188, 300, 30, true);
    lv_obj_t *ls = lv_label_create(bs);
    lv_label_set_text(ls, "Guardar NVS");
    lv_obj_center(ls);
    lv_obj_add_event_cb(bs, [](lv_event_t *e) {
        (void)e;
        uint16_t i = lv_roller_get_selected(g_roll_st);
        if (i > 4) {
            i = 4;
        }
        DataModel::getInstance().lock();
        SystemSettings ss = DataModel::getInstance().getSettings();
        DataModel::getInstance().unlock();
        ss.ph_target[i]  = lv_slider_get_value(g_sl_ph) / 100.0f;
        ss.tds_target[i] = lv_slider_get_value(g_sl_tds);
        DataModel::getInstance().updateSettings(ss);
        settingsSaveFrom(ss);
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(g_roll_st, [](lv_event_t *e) {
        if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
            return;
        }
        uint16_t i = lv_roller_get_selected(g_roll_st);
        if (i > 4) {
            i = 4;
        }
        DataModel::getInstance().lock();
        SystemSettings &ss = DataModel::getInstance().getSettings();
        int ph = static_cast<int>(ss.ph_target[i] * 100.0f);
        int td = ss.tds_target[i];
        DataModel::getInstance().unlock();
        lv_slider_set_value(g_sl_ph, ph, LV_ANIM_OFF);
        lv_slider_set_value(g_sl_tds, td, LV_ANIM_OFF);
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    g_scr[SCR_SET_STAGE] = s;
}

static lv_obj_t *g_sl_saf_ph = nullptr;
static lv_obj_t *g_sl_saf_ppm = nullptr;

void build_set_safety() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_add_style(s, &sty_bg, 0);
    lv_obj_add_event_cb(s, on_act, LV_EVENT_PRESSED, nullptr);
    hdr(s, "Seguridad", SCR_SETTINGS_HUB);
    lv_obj_t *h = lv_label_create(s);
    lv_label_set_text(h, "Limites locales de visualizacion/alarma.");
    lv_obj_set_pos(h, 8, 32);
    g_sl_saf_ph = lv_slider_create(s);
    lv_slider_set_range(g_sl_saf_ph, 70, 100);
    lv_obj_set_pos(g_sl_saf_ph, 8, 56);
    lv_obj_set_size(g_sl_saf_ph, 280, 12);
    g_sl_saf_ppm = lv_slider_create(s);
    lv_slider_set_range(g_sl_saf_ppm, 500, 4000);
    lv_obj_set_pos(g_sl_saf_ppm, 8, 88);
    lv_obj_set_size(g_sl_saf_ppm, 280, 12);
    lv_obj_t *bs = mk_btn(s, 8, 190, 300, 30, true);
    lv_obj_t *ls = lv_label_create(bs);
    lv_label_set_text(ls, "Guardar");
    lv_obj_center(ls);
    lv_obj_add_event_cb(bs, [](lv_event_t *e) {
        (void)e;
        DataModel::getInstance().lock();
        SystemSettings &ss = DataModel::getInstance().getSettings();
        ss.safety_ph_ceiling  = lv_slider_get_value(g_sl_saf_ph) / 10.0f;
        ss.safety_ppm_ceiling = static_cast<uint16_t>(lv_slider_get_value(g_sl_saf_ppm));
        settingsSaveFrom(ss);
        DataModel::getInstance().unlock();
    }, LV_EVENT_CLICKED, nullptr);
    g_scr[SCR_SET_SAFETY] = s;
}

static lv_obj_t *g_sl_r_on, *g_sl_r_off;

void build_set_recirc() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_add_style(s, &sty_bg, 0);
    lv_obj_add_event_cb(s, on_act, LV_EVENT_PRESSED, nullptr);
    hdr(s, "Recirculacion", SCR_SETTINGS_HUB);
    g_sl_r_on  = lv_slider_create(s);
    g_sl_r_off = lv_slider_create(s);
    lv_slider_set_range(g_sl_r_on, 30, 3600);
    lv_slider_set_range(g_sl_r_off, 30, 3600);
    lv_obj_set_pos(g_sl_r_on, 8, 40);
    lv_obj_set_pos(g_sl_r_off, 8, 80);
    lv_obj_set_size(g_sl_r_on, 280, 12);
    lv_obj_set_size(g_sl_r_off, 280, 12);
    lv_obj_t *bs = mk_btn(s, 8, 190, 300, 30, true);
    lv_obj_t *ls = lv_label_create(bs);
    lv_label_set_text(ls, "Guardar");
    lv_obj_center(ls);
    lv_obj_add_event_cb(bs, [](lv_event_t *e) {
        (void)e;
        DataModel::getInstance().lock();
        SystemSettings &ss = DataModel::getInstance().getSettings();
        ss.recirc_on_sec  = static_cast<uint32_t>(lv_slider_get_value(g_sl_r_on));
        ss.recirc_off_sec = static_cast<uint32_t>(lv_slider_get_value(g_sl_r_off));
        settingsSaveFrom(ss);
        DataModel::getInstance().unlock();
    }, LV_EVENT_CLICKED, nullptr);
    g_scr[SCR_SET_RECIRC] = s;
}

static lv_obj_t *g_sl_stab;

void build_set_stab() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_add_style(s, &sty_bg, 0);
    lv_obj_add_event_cb(s, on_act, LV_EVENT_PRESSED, nullptr);
    hdr(s, "Estabilizacion", SCR_SETTINGS_HUB);
    g_sl_stab = lv_slider_create(s);
    lv_slider_set_range(g_sl_stab, 30, 900);
    lv_obj_set_pos(g_sl_stab, 8, 40);
    lv_obj_set_size(g_sl_stab, 280, 12);
    lv_obj_t *bs = mk_btn(s, 8, 190, 300, 30, true);
    lv_obj_t *ls = lv_label_create(bs);
    lv_label_set_text(ls, "Guardar");
    lv_obj_center(ls);
    lv_obj_add_event_cb(bs, [](lv_event_t *e) {
        (void)e;
        DataModel::getInstance().lock();
        SystemSettings &ss = DataModel::getInstance().getSettings();
        ss.stabilize_sec = static_cast<uint32_t>(lv_slider_get_value(g_sl_stab));
        settingsSaveFrom(ss);
        DataModel::getInstance().unlock();
    }, LV_EVENT_CLICKED, nullptr);
    g_scr[SCR_SET_STAB] = s;
}

static lv_obj_t *g_sl_mic;

void build_set_micro() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_add_style(s, &sty_bg, 0);
    lv_obj_add_event_cb(s, on_act, LV_EVENT_PRESSED, nullptr);
    hdr(s, "Microdosis ml", SCR_SETTINGS_HUB);
    g_sl_mic = lv_slider_create(s);
    lv_slider_set_range(g_sl_mic, 1, 50);
    lv_obj_set_pos(g_sl_mic, 8, 40);
    lv_obj_set_size(g_sl_mic, 280, 12);
    lv_obj_t *bs = mk_btn(s, 8, 190, 300, 30, true);
    lv_obj_t *ls = lv_label_create(bs);
    lv_label_set_text(ls, "Guardar");
    lv_obj_center(ls);
    lv_obj_add_event_cb(bs, [](lv_event_t *e) {
        (void)e;
        DataModel::getInstance().lock();
        SystemSettings &ss = DataModel::getInstance().getSettings();
        ss.microdose_ml = static_cast<float>(lv_slider_get_value(g_sl_mic)) / 10.0f;
        settingsSaveFrom(ss);
        DataModel::getInstance().unlock();
    }, LV_EVENT_CLICKED, nullptr);
    g_scr[SCR_SET_MICRO] = s;
}

static lv_obj_t *g_sl_ph_on, *g_sl_ph_h;

void build_set_photo() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_add_style(s, &sty_bg, 0);
    lv_obj_add_event_cb(s, on_act, LV_EVENT_PRESSED, nullptr);
    hdr(s, "Fotoperiodo", SCR_SETTINGS_HUB);
    g_sl_ph_on = lv_slider_create(s);
    g_sl_ph_h  = lv_slider_create(s);
    lv_slider_set_range(g_sl_ph_on, 0, 23);
    lv_slider_set_range(g_sl_ph_h, 6, 24);
    lv_obj_set_pos(g_sl_ph_on, 8, 40);
    lv_obj_set_pos(g_sl_ph_h, 8, 80);
    lv_obj_set_size(g_sl_ph_on, 280, 12);
    lv_obj_set_size(g_sl_ph_h, 280, 12);
    lv_obj_t *bs = mk_btn(s, 8, 190, 300, 30, true);
    lv_obj_t *ls = lv_label_create(bs);
    lv_label_set_text(ls, "Guardar");
    lv_obj_center(ls);
    lv_obj_add_event_cb(bs, [](lv_event_t *e) {
        (void)e;
        DataModel::getInstance().lock();
        SystemSettings &ss = DataModel::getInstance().getSettings();
        ss.photo_on_hour = static_cast<uint8_t>(lv_slider_get_value(g_sl_ph_on));
        ss.photo_hours   = static_cast<uint8_t>(lv_slider_get_value(g_sl_ph_h));
        settingsSaveFrom(ss);
        DataModel::getInstance().unlock();
    }, LV_EVENT_CLICKED, nullptr);
    g_scr[SCR_SET_PHOTO] = s;
}

static lv_obj_t *g_sl_von, *g_sl_voff;

void build_set_vent() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_add_style(s, &sty_bg, 0);
    lv_obj_add_event_cb(s, on_act, LV_EVENT_PRESSED, nullptr);
    hdr(s, "Ventilacion", SCR_SETTINGS_HUB);
    g_sl_von  = lv_slider_create(s);
    g_sl_voff = lv_slider_create(s);
    lv_slider_set_range(g_sl_von, 200, 400);
    lv_slider_set_range(g_sl_voff, 150, 350);
    lv_obj_set_pos(g_sl_von, 8, 40);
    lv_obj_set_pos(g_sl_voff, 8, 80);
    lv_obj_set_size(g_sl_von, 280, 12);
    lv_obj_set_size(g_sl_voff, 280, 12);
    lv_obj_t *bs = mk_btn(s, 8, 190, 300, 30, true);
    lv_obj_t *ls = lv_label_create(bs);
    lv_label_set_text(ls, "Guardar");
    lv_obj_center(ls);
    lv_obj_add_event_cb(bs, [](lv_event_t *e) {
        (void)e;
        DataModel::getInstance().lock();
        SystemSettings &ss = DataModel::getInstance().getSettings();
        ss.vent_temp_on_c  = lv_slider_get_value(g_sl_von) / 10.0f;
        ss.vent_temp_off_c = lv_slider_get_value(g_sl_voff) / 10.0f;
        settingsSaveFrom(ss);
        DataModel::getInstance().unlock();
    }, LV_EVENT_CLICKED, nullptr);
    g_scr[SCR_SET_VENT] = s;
}

void build_set_comm() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_add_style(s, &sty_bg, 0);
    lv_obj_add_event_cb(s, on_act, LV_EVENT_PRESSED, nullptr);
    hdr(s, "Comunicacion", SCR_SETTINGS_HUB);
    lv_obj_t *t = lv_label_create(s);
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(t, 300);
    lv_label_set_text(t,
                        "UART2 hacia Mega: baud y pines en NVS (requiere reinicio).\n"
                        "WiFi: estado en tiempo real abajo.");
    lv_obj_set_pos(t, 8, 32);
    lv_obj_t *bs = mk_btn(s, 8, 190, 300, 30, false);
    lv_obj_t *ls = lv_label_create(bs);
    lv_label_set_text(ls, "Guardar baud 115200");
    lv_obj_center(ls);
    lv_obj_add_event_cb(bs, [](lv_event_t *e) {
        (void)e;
        DataModel::getInstance().lock();
        DataModel::getInstance().getSettings().uart_baud = 115200;
        settingsSaveFrom(DataModel::getInstance().getSettings());
        DataModel::getInstance().unlock();
    }, LV_EVENT_CLICKED, nullptr);
    wcomm.wf = lv_label_create(s);
    lv_label_set_long_mode(wcomm.wf, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(wcomm.wf, 300);
    lv_obj_set_pos(wcomm.wf, 8, 132);
    lv_label_set_text(wcomm.wf, "WiFi: …");
    g_scr[SCR_SET_COMM] = s;
}

static lv_obj_t *g_sw_dark, *g_sl_idle;

void build_set_display() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_add_style(s, &sty_bg, 0);
    lv_obj_add_event_cb(s, on_act, LV_EVENT_PRESSED, nullptr);
    hdr(s, "Pantalla", SCR_SETTINGS_HUB);
    lv_obj_t *ld = lv_label_create(s);
    lv_label_set_text(ld, "Tema oscuro");
    lv_obj_set_pos(ld, 8, 36);
    g_sw_dark = lv_switch_create(s);
    lv_obj_set_pos(g_sw_dark, 200, 32);
    lv_obj_t *li = lv_label_create(s);
    lv_label_set_text(li, "Reposo (ms)");
    lv_obj_set_pos(li, 8, 72);
    g_sl_idle = lv_slider_create(s);
    lv_slider_set_range(g_sl_idle, 20000, 600000);
    lv_obj_set_pos(g_sl_idle, 8, 92);
    lv_obj_set_size(g_sl_idle, 280, 12);
    lv_obj_t *bs = mk_btn(s, 8, 190, 300, 30, true);
    lv_obj_t *ls = lv_label_create(bs);
    lv_label_set_text(ls, "Guardar");
    lv_obj_center(ls);
    lv_obj_add_event_cb(bs, [](lv_event_t *e) {
        (void)e;
        DataModel::getInstance().lock();
        SystemSettings &ss = DataModel::getInstance().getSettings();
        ss.dark_theme         = lv_obj_has_state(g_sw_dark, LV_STATE_CHECKED);
        ss.ui_idle_timeout_ms = static_cast<uint32_t>(lv_slider_get_value(g_sl_idle));
        settingsSaveFrom(ss);
        DataModel::getInstance().unlock();
        s_theme_rebuild = true;
    }, LV_EVENT_CLICKED, nullptr);
    g_scr[SCR_SET_DISPLAY] = s;
}

static lv_obj_t *g_sw_snd;

void build_set_sound() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_add_style(s, &sty_bg, 0);
    lv_obj_add_event_cb(s, on_act, LV_EVENT_PRESSED, nullptr);
    hdr(s, "Sonido", SCR_SETTINGS_HUB);
    lv_obj_t *ld = lv_label_create(s);
    lv_label_set_text(ld, "Alertas habilitadas");
    lv_obj_set_pos(ld, 8, 40);
    g_sw_snd = lv_switch_create(s);
    lv_obj_set_pos(g_sw_snd, 220, 36);
    lv_obj_t *bs = mk_btn(s, 8, 190, 300, 30, true);
    lv_obj_t *ls = lv_label_create(bs);
    lv_label_set_text(ls, "Guardar");
    lv_obj_center(ls);
    lv_obj_add_event_cb(bs, [](lv_event_t *e) {
        (void)e;
        DataModel::getInstance().lock();
        DataModel::getInstance().getSettings().sound_enabled =
            lv_obj_has_state(g_sw_snd, LV_STATE_CHECKED);
        settingsSaveFrom(DataModel::getInstance().getSettings());
        DataModel::getInstance().unlock();
    }, LV_EVENT_CLICKED, nullptr);
    g_scr[SCR_SET_SOUND] = s;
}

void build_maint() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_add_style(s, &sty_bg, 0);
    lv_obj_add_event_cb(s, on_act, LV_EVENT_PRESSED, nullptr);
    hdr(s, "Mantenimiento", SCR_HOME);
    wm.u = lv_label_create(s);
    lv_obj_set_pos(wm.u, 8, 200);
    int y = 32;

    lv_obj_t *b1 = mk_btn(s, 8, y, 300, 22, false);
    lv_obj_t *l1 = lv_label_create(b1);
    lv_label_set_text(l1, "Test pantalla (toque=ciclo, long=mant.)");
    lv_obj_set_style_text_font(l1, F_S, 0);
    lv_obj_center(l1);
    lv_obj_add_event_cb(
        b1,
        [](lv_event_t *e) {
            (void)e;
            lv_obj_t *o = lv_obj_create(nullptr);
            lv_obj_add_style(o, &sty_bg, 0);
            lv_obj_set_style_bg_color(o, C_ERR, 0);
            lv_scr_load(o);
            lv_obj_add_event_cb(
                o,
                [](lv_event_t *ev) {
                    lv_obj_t *oo = lv_event_get_target(ev);
                    static uint8_t st = 0;
                    st                = static_cast<uint8_t>((st + 1) % 3);
                    if (st == 0) {
                        lv_obj_set_style_bg_color(oo, C_ERR, 0);
                    } else if (st == 1) {
                        lv_obj_set_style_bg_color(oo, C_OK, 0);
                    } else {
                        lv_obj_set_style_bg_color(oo, C_ACC, 0);
                    }
                },
                LV_EVENT_CLICKED, nullptr);
            lv_obj_add_event_cb(
                o,
                [](lv_event_t *ev) {
                    (void)ev;
                    UiManager::getInstance().loadScreen(SCR_MAINTENANCE);
                },
                LV_EVENT_LONG_PRESSED, nullptr);
        },
        LV_EVENT_CLICKED, nullptr);
    y += 26;

    lv_obj_t *b2 = mk_btn(s, 8, y, 300, 22, false);
    lv_obj_t *l2 = lv_label_create(b2);
    lv_label_set_text(l2, "Test UART (GET_STATUS)");
    lv_obj_set_style_text_font(l2, F_S, 0);
    lv_obj_center(l2);
    lv_obj_add_event_cb(
        b2,
        [](lv_event_t *e) {
            (void)e;
            send_cmd("CMD:GET_STATUS");
            if (wm.u) {
                lv_label_set_text(wm.u, "UART: enviado GET_STATUS");
            }
        },
        LV_EVENT_CLICKED, nullptr);
    y += 26;

    lv_obj_t *b3 = mk_btn(s, 8, y, 300, 22, false);
    lv_obj_t *l3 = lv_label_create(b3);
    lv_label_set_text(l3, "Test sensores");
    lv_obj_center(l3);
    lv_obj_add_event_cb(
        b3,
        [](lv_event_t *e) {
            (void)e;
            UiManager::getInstance().loadScreen(SCR_SENSORS);
        },
        LV_EVENT_CLICKED, nullptr);
    y += 26;

    lv_obj_t *b4 = mk_btn(s, 8, y, 300, 22, false);
    lv_obj_t *l4 = lv_label_create(b4);
    lv_label_set_text(l4, "Test actuadores / rele");
    lv_obj_center(l4);
    lv_obj_add_event_cb(
        b4,
        [](lv_event_t *e) {
            (void)e;
            UiManager::getInstance().loadScreen(SCR_ACTUATORS);
        },
        LV_EVENT_CLICKED, nullptr);
    y += 26;

    lv_obj_t *b5 = mk_btn(s, 8, y, 300, 22, false);
    lv_obj_t *l5 = lv_label_create(b5);
    lv_label_set_text(l5, "Test assets (ver log serie)");
    lv_obj_center(l5);
    lv_obj_add_event_cb(
        b5,
        [](lv_event_t *e) {
            (void)e;
            assets_fs_log_startup_report();
            if (wm.u) {
                lv_label_set_text(wm.u, "Assets: log enviado a Serial");
            }
        },
        LV_EVENT_CLICKED, nullptr);
    y += 26;

    lv_obj_t *b6 = mk_btn(s, 8, y, 300, 22, true);
    lv_obj_t *l6 = lv_label_create(b6);
    lv_label_set_text(l6, "Info firmware");
    lv_obj_center(l6);
    lv_obj_add_event_cb(
        b6,
        [](lv_event_t *e) {
            (void)e;
            UiManager::getInstance().loadScreen(SCR_SYSTEM_INFO);
        },
        LV_EVENT_CLICKED, nullptr);

    g_scr[SCR_MAINTENANCE] = s;
}

void build_info() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_add_style(s, &sty_bg, 0);
    hdr(s, "Sistema", SCR_HOME);
    wi.t = lv_label_create(s);
    lv_label_set_long_mode(wi.t, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(wi.t, 300);
    lv_obj_set_pos(wi.t, 8, 32);
    g_scr[SCR_SYSTEM_INFO] = s;
}

void do_build(ScreenId id) {
    switch (id) {
        case SCR_SPLASH:
            build_splash();
            break;
        case SCR_HOME:
            build_home();
            break;
        case SCR_DASHBOARD:
            build_dashboard();
            break;
        case SCR_STAGES:
            build_stages();
            break;
        case SCR_SENSORS:
            build_sensors();
            break;
        case SCR_ACTUATORS:
            build_actuators();
            break;
        case SCR_AUTOMATION:
            build_automation();
            break;
        case SCR_CALIBRATION:
            build_calibration();
            break;
        case SCR_ALARMS:
            build_alarms();
            break;
        case SCR_SETTINGS_HUB:
            build_settings_hub();
            break;
        case SCR_SET_STAGE:
            build_set_stage_v2();
            break;
        case SCR_SET_SAFETY:
            build_set_safety();
            break;
        case SCR_SET_RECIRC:
            build_set_recirc();
            break;
        case SCR_SET_STAB:
            build_set_stab();
            break;
        case SCR_SET_MICRO:
            build_set_micro();
            break;
        case SCR_SET_PHOTO:
            build_set_photo();
            break;
        case SCR_SET_VENT:
            build_set_vent();
            break;
        case SCR_SET_COMM:
            build_set_comm();
            break;
        case SCR_SET_DISPLAY:
            build_set_display();
            break;
        case SCR_SET_SOUND:
            build_set_sound();
            break;
        case SCR_MAINTENANCE:
            build_maint();
            break;
        case SCR_SYSTEM_INFO:
            build_info();
            break;
        default:
            break;
    }
}

void tick_dashboard(const SystemState &st, const SystemSettings &se, uint32_t now) {
    (void)se;
    int si = clamp_stage(st.active_stage);
    const StageProfile &sp = kStageProfiles[si];
    lblf(wd.st, "Etapa: %s", sp.name);
    lblf(wd.md, "Modo: %s%s", st.maintenance_mode ? "MANT" : (st.auto_mode ? "AUTO" : "MAN"),
         st.uart_connected ? "" : "  UART!");
    lblf(wd.ph, "%s", fmt_ph(st));
    lblf(wd.ec, "%s", fmt_ec(st));
    if (!st.telemetry_live) {
        lv_label_set_text(wd.tw, "T.agua: Sin enlace");
        lv_label_set_text(wd.ta, "T.aire: —");
        lv_label_set_text(wd.ha, "HR: —");
    } else {
        if (!st.tw_probe_ok) {
            lv_label_set_text(wd.tw, "T.agua: lectura invalida");
        } else {
            lblf(wd.tw, "T.agua: %.1f C", st.temp_water);
        }
        if (st.dht_online) {
            lblf(wd.ta, "T.aire: %.1f C", st.temp_air);
            lblf(wd.ha, "HR: %.0f %%", st.hum_air);
        } else {
            lv_label_set_text(wd.ta, "T.aire: DHT off");
            lv_label_set_text(wd.ha, "HR: DHT off");
        }
    }
    lblf(wd.lv, "Nivel: %s  min=%s max=%s", st.level_min ? "BAJO" : (st.level_max ? "ALTO" : "OK"),
         st.level_min ? "ON" : "ok", st.level_max ? "ON" : "ok");
    if (st.current_alarm > 0) {
        lblf(wd.al, LV_SYMBOL_WARNING " %s", st.alarm_message);
        lv_obj_set_style_text_color(wd.al, C_ERR, 0);
    } else {
        lv_label_set_text(wd.al, LV_SYMBOL_OK " Sin alarmas activas");
        lv_obj_set_style_text_color(wd.al, C_OK, 0);
    }
    lblf(wd.ac, "Ultima accion: %s", st.last_action);
    lblf(wd.qk,
         "A:%s B:%s R:%s In:%s L:%s | %lus desde telemetria",
         st.state_pump_a ? "ON" : "off", st.state_pump_b ? "ON" : "off",
         st.state_recirculation ? "ON" : "off", st.state_pump_in ? "ON" : "off",
         st.state_light ? "ON" : "off", (unsigned long)((now - st.uart_last_rx_ms) / 1000UL));
}

const char *fmt_ph(const SystemState &st) {
    static char b[32];
    if (!st.telemetry_live) {
        return "pH: Sin enlace UART";
    }
    if (!st.ph_probe_ok) {
        return "pH: lectura invalida";
    }
    snprintf(b, sizeof(b), "pH: %.2f (obj %.2f-%.2f)", st.ph,
             kStageProfiles[clamp_stage(st.active_stage)].ph_min,
             kStageProfiles[clamp_stage(st.active_stage)].ph_max);
    return b;
}

const char *fmt_ec(const SystemState &st) {
    static char b[40];
    if (!st.telemetry_live) {
        return "EC/TDS: Sin enlace";
    }
    if (!st.tds_probe_ok) {
        return "EC/TDS: invalido";
    }
    snprintf(b, sizeof(b), "EC: %.2f mS  TDS %d ppm", st.tds / 500.0f, st.tds);
    return b;
}

void tick_sensors(const SystemState &st, uint32_t now) {
    lblf(ws.ln[0], "%s", fmt_ph(st));
    lblf(ws.ln[1], "%s", fmt_ec(st));
    lblf(ws.ln[2], "T agua: %s",
         !st.telemetry_live ? "Sin enlace"
                            : (st.tw_probe_ok ? "valida" : "invalida"));
    lblf(ws.ln[3], "T agua: %.1f C", st.temp_water);
    lblf(ws.ln[4], "T aire: %s",
         st.dht_online ? "valida" : "sin DHT");
    lblf(ws.ln[5], "Humedad: %.0f %%", st.hum_air);
    lblf(ws.ln[6], "Nivel min: %s  max: %s", st.level_min ? "ACTIVO" : "ok",
         st.level_max ? "ACTIVO" : "ok");
    lblf(ws.ln[7], "RTC Mega: %s  Hora: %s", st.rtc_online ? "OK" : "no OK",
         st.controller_clock[0] ? st.controller_clock : "(en STS)");
    lblf(ws.ln[8], "Lectura: %s  hace %lus", st.telemetry_live ? "viva" : "sin datos",
         (unsigned long)((now - st.uart_last_rx_ms) / 1000UL));
}

void tick_auto(const SystemState &st, uint32_t now) {
    lblf(wa.md, "MAN=%d AUTO=%d MANT=%d", !st.auto_mode && !st.maintenance_mode, st.auto_mode,
         st.maintenance_mode);
    const char *fs = megaStateStr(st.mega_machine_state);
    lblf(wa.fsm, "Estado Mega: %s", fs);
    lblf(wa.ac, "Paso (ACT): %s", st.last_action);
    lblf(wa.tel, "Telemetria hace: %lu s | Prox. ciclo: ver Mega",
         (unsigned long)((now - st.uart_last_rx_ms) / 1000UL));
    lblf(wa.co, "Correcciones pH:%u EC:%u (Mega)", (unsigned)st.ph_corrections_mega,
         (unsigned)st.tds_corrections_mega);
    String blk;
    if (st.maintenance_mode) {
        blk += "MANT: dosificacion manual permitida. ";
    }
    if (st.auto_mode && !st.maintenance_mode) {
        blk += "AUTO: interlock A/B en Mega; ESP no fuerza OUT. ";
    }
    if (!st.rtc_online && st.auto_mode) {
        blk += "RTC requerido para AUTO fiable. ";
    }
    if (st.level_max) {
        blk += "Nivel alto: puede bloquear llenado. ";
    }
    lv_label_set_text(wa.blk, blk.length() ? blk.c_str() : "Sin bloqueos reportados.");
}

void tick_act(const SystemState &st) {
    bool dis = st.auto_mode && !st.maintenance_mode;
    lv_label_set_text(wact.wrn,
                      dis ? "AUTO activo: conmutacion deshabilitada."
                          : "MAN/MANT: conmutacion habilitada.");
    s_act_sync = true;
    const bool vals[] = {st.state_pump_a,  st.state_pump_b,   st.state_ph_up,
                         st.state_ph_down, st.state_recirculation, st.state_pump_in,
                         st.state_light,   st.state_intractor, st.state_extractor,
                         st.state_buzzer};
    for (int i = 0; i < 10; ++i) {
        if (!wact.sw[i]) {
            continue;
        }
        if (vals[i]) {
            lv_obj_add_state(wact.sw[i], LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(wact.sw[i], LV_STATE_CHECKED);
        }
        if (dis) {
            lv_obj_add_state(wact.sw[i], LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(wact.sw[i], LV_STATE_DISABLED);
        }
    }
    s_act_sync = false;
}

void tick_alm(const SystemState &st) {
    if (st.current_alarm > 0) {
        lblf(wal.st, "ACTIVA: %s", st.alarm_message);
        lv_obj_set_style_text_color(wal.st, C_ERR, 0);
    } else {
        lv_label_set_text(wal.st, "Ninguna alarma activa.");
        lv_obj_set_style_text_color(wal.st, C_OK, 0);
    }
    char hist[256] = "Historial:\n";
    size_t pos = strlen(hist);
    for (int i = 0; i < st.alarm_history_count && i < SystemState::kAlarmHist; ++i) {
        int n = snprintf(hist + pos, sizeof(hist) - pos, " - %s\n", st.alarm_history[i]);
        if (n > 0) {
            pos += static_cast<size_t>(n);
            if (pos >= sizeof(hist)) {
                break;
            }
        }
    }
    lv_label_set_text(wal.hi, hist);
}

void tick_cal(const SystemSettings &se) {
    if (!wcal.sv) {
        return;
    }
    lblf(wcal.sv,
         "pH off=%.3f slope=%.3f\nTDS factor=%.4f\nRefs %.2f / %.2f", se.ph_offset,
         se.ph_slope, se.tds_cal_factor, se.ph_cal_ref1, se.ph_cal_ref2);
}

void sync_settings_sliders() {
    DataModel::getInstance().lock();
    SystemSettings se = DataModel::getInstance().getSettings();
    DataModel::getInstance().unlock();
    if (g_sl_ph && g_sl_tds && g_roll_st) {
        uint16_t i = lv_roller_get_selected(g_roll_st);
        if (i > 4) {
            i = 4;
        }
        lv_slider_set_value(g_sl_ph, static_cast<int>(se.ph_target[i] * 100.0f), LV_ANIM_OFF);
        lv_slider_set_value(g_sl_tds, se.tds_target[i], LV_ANIM_OFF);
    }
    if (g_sl_saf_ph) {
        lv_slider_set_value(g_sl_saf_ph, static_cast<int>(se.safety_ph_ceiling * 10.0f), LV_ANIM_OFF);
        lv_slider_set_value(g_sl_saf_ppm, se.safety_ppm_ceiling, LV_ANIM_OFF);
    }
    if (g_sl_r_on) {
        lv_slider_set_value(g_sl_r_on, static_cast<int>(se.recirc_on_sec), LV_ANIM_OFF);
        lv_slider_set_value(g_sl_r_off, static_cast<int>(se.recirc_off_sec), LV_ANIM_OFF);
    }
    if (g_sl_stab) {
        lv_slider_set_value(g_sl_stab, static_cast<int>(se.stabilize_sec), LV_ANIM_OFF);
    }
    if (g_sl_mic) {
        lv_slider_set_value(g_sl_mic, static_cast<int>(se.microdose_ml * 10.0f), LV_ANIM_OFF);
    }
    if (g_sl_ph_on) {
        lv_slider_set_value(g_sl_ph_on, se.photo_on_hour, LV_ANIM_OFF);
        lv_slider_set_value(g_sl_ph_h, se.photo_hours, LV_ANIM_OFF);
    }
    if (g_sl_von) {
        lv_slider_set_value(g_sl_von, static_cast<int>(se.vent_temp_on_c * 10.0f), LV_ANIM_OFF);
        lv_slider_set_value(g_sl_voff, static_cast<int>(se.vent_temp_off_c * 10.0f), LV_ANIM_OFF);
    }
    if (g_sw_dark) {
        if (se.dark_theme) {
            lv_obj_add_state(g_sw_dark, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(g_sw_dark, LV_STATE_CHECKED);
        }
        lv_slider_set_value(g_sl_idle, static_cast<int>(se.ui_idle_timeout_ms), LV_ANIM_OFF);
    }
    if (g_sw_snd) {
        if (se.sound_enabled) {
            lv_obj_add_state(g_sw_snd, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(g_sw_snd, LV_STATE_CHECKED);
        }
    }
    if (g_sl_coff && g_sl_cslope) {
        lv_slider_set_value(g_sl_coff, static_cast<int>(se.ph_offset * 100.0f), LV_ANIM_OFF);
        lv_slider_set_value(g_sl_cslope, static_cast<int>(se.ph_slope * 100.0f), LV_ANIM_OFF);
    }
    if (g_sl_tdsf) {
        int v = static_cast<int>(se.tds_cal_factor * 100.0f);
        if (v < 50) {
            v = 50;
        }
        if (v > 400) {
            v = 400;
        }
        lv_slider_set_value(g_sl_tdsf, v, LV_ANIM_OFF);
    }
}

void tick_info(const SystemState &st) {
    if (!wi.t) {
        return;
    }
    lblf(wi.t,
         "HMI %s\nProto UART %s\nAssets %s\nUptime %lus\nHeap %u\nLittleFS %s\nPaq. UART ok:%lu",
         EWEED_HMI_VERSION, EWEED_UART_PROTOCOL_VERSION, EWEED_ASSETS_BUNDLE_REV,
         (unsigned long)(millis() / 1000UL), (unsigned)ESP.getFreeHeap(),
         assets_fs_ready() ? "montado" : "error", (unsigned long)st.uart_ok_packets);
}

}  // namespace

void ui_render_bump_activity() { g_activity_ms = millis(); }

lv_obj_t *ui_render_screen_ptr(ScreenId id) {
    if (id >= SCR_COUNT) {
        return nullptr;
    }
    return g_scr[id];
}

void ui_render_destroy_all() {
    for (int i = 0; i < SCR_COUNT; ++i) {
        if (g_scr[i]) {
            lv_obj_del(g_scr[i]);
            g_scr[i] = nullptr;
        }
    }
    memset(&wd, 0, sizeof(wd));
    memset(&ws, 0, sizeof(ws));
    memset(&wa, 0, sizeof(wa));
    memset(&wact, 0, sizeof(wact));
    memset(&wal, 0, sizeof(wal));
    memset(&wcal, 0, sizeof(wcal));
    memset(&wm, 0, sizeof(wm));
    memset(&wi, 0, sizeof(wi));
    g_stg_txt   = nullptr;
    g_roll_st   = nullptr;
    g_sl_ph = g_sl_tds = nullptr;
    g_sl_saf_ph = g_sl_saf_ppm = nullptr;
    g_sl_r_on = g_sl_r_off = nullptr;
    g_sl_stab = nullptr;
    g_sl_mic = nullptr;
    g_sl_ph_on = g_sl_ph_h = nullptr;
    g_sl_von = g_sl_voff = nullptr;
    g_sw_dark = g_sl_idle = nullptr;
    g_sw_snd          = nullptr;
    g_sl_coff         = nullptr;
    g_sl_cslope       = nullptr;
    g_sl_tdsf         = nullptr;
    wcomm.wf          = nullptr;
}

void ui_render_init() {
    DataModel::getInstance().lock();
    s_dark = DataModel::getInstance().getSettings().dark_theme;
    DataModel::getInstance().unlock();
    styles_init();
    ui_render_destroy_all();
    s_tick_prev = millis();
}

void ui_render_rebuild_theme() {
    s_theme_rebuild = true;
}

void ui_render_ensure(ScreenId id) {
    if (id >= SCR_COUNT || g_scr[id]) {
        return;
    }
    do_build(id);
}

void ui_render_tick(uint32_t now) {
    if (s_theme_rebuild) {
        s_theme_rebuild = false;
        ScreenId r = UiManager::getInstance().currentScreen;
        styles_init();
        ui_render_destroy_all();
        UiManager::getInstance().loadScreen(r);
        return;
    }

    DataModel::getInstance().lock();
    bool dk = DataModel::getInstance().getSettings().dark_theme;
    DataModel::getInstance().unlock();
    if (dk != s_dark) {
        styles_init();
        ui_render_destroy_all();
        UiManager::getInstance().loadScreen(UiManager::getInstance().currentScreen);
        return;
    }

    if (now - s_tick_prev < 250) {
        return;
    }
    s_tick_prev = now;

    DataModel::getInstance().lock();
    SystemState    st = DataModel::getInstance().getState();
    SystemSettings se = DataModel::getInstance().getSettings();
    DataModel::getInstance().unlock();

    if (strcmp(s_act_snap, st.last_action) != 0) {
        strncpy(s_act_snap, st.last_action, sizeof(s_act_snap) - 1);
        s_act_snap_ms = now;
    }

    uint32_t idle_to = se.ui_idle_timeout_ms;
    if (idle_to >= 20000 && UiManager::getInstance().currentScreen != SCR_SPLASH &&
        (now - g_activity_ms > idle_to)) {
        UiManager::getInstance().loadScreen(SCR_SPLASH);
        g_activity_ms = now;
    }

    if (UiManager::getInstance().currentScreen == SCR_SPLASH) {
        return;
    }

    if (g_scr[SCR_DASHBOARD]) {
        tick_dashboard(st, se, now);
    }
    if (g_scr[SCR_SENSORS]) {
        tick_sensors(st, now);
    }
    if (g_scr[SCR_AUTOMATION]) {
        tick_auto(st, now);
    }
    if (g_scr[SCR_ACTUATORS]) {
        tick_act(st);
    }
    if (g_scr[SCR_ALARMS]) {
        tick_alm(st);
    }
    if (g_scr[SCR_CALIBRATION]) {
        tick_cal(se);
    }
    if (g_scr[SCR_SYSTEM_INFO]) {
        tick_info(st);
    }
    if (g_scr[SCR_SET_COMM] && wcomm.wf) {
        lblf(wcomm.wf, "WiFi: %s  %s\nIP: %s  RSSI:%d dBm",
             st.wifi_ap_mode ? "AP" : "STA", st.wifi_connected ? "conectado" : "desconectado",
             st.wifi_ip[0] ? st.wifi_ip : "-", (int)st.wifi_rssi);
    }

    static ScreenId prev_screen = SCR_COUNT;
    ScreenId        cur         = UiManager::getInstance().currentScreen;
    if (cur != prev_screen) {
        prev_screen = cur;
        sync_settings_sliders();
    }
}
