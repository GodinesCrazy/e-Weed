#include "ui/ui_render.h"
#include "assets_fs.h"
#include "comms/uart_comm.h"
#include "data_model.h"
#include "eweed/fw_version.h"
#include "storage/settings_store.h"

#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

static void ui_ascii_text(const char *in, char *out, size_t outSize) {
    if (!out || outSize == 0) return;
    if (!in) {
        out[0] = '\0';
        return;
    }

    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j + 1 < outSize;) {
        const unsigned char c = static_cast<unsigned char>(in[i]);

        if (c < 128) {
            out[j++] = static_cast<char>(c);
            ++i;
            continue;
        }

        const unsigned char c2 = static_cast<unsigned char>(in[i + 1]);
        const unsigned char c3 = static_cast<unsigned char>(in[i + 2]);

        if (c == 0xC3) {
            char repl = 0;
            switch (c2) {
                case 0x81: case 0xA1: repl = 'a'; break; // A/a con acento
                case 0x89: case 0xA9: repl = 'e'; break; // E/e con acento
                case 0x8D: case 0xAD: repl = 'i'; break; // I/i con acento
                case 0x93: case 0xB3: repl = 'o'; break; // O/o con acento
                case 0x9A: case 0xBA: repl = 'u'; break; // U/u con acento
                case 0x9C: case 0xBC: repl = 'u'; break; // U/u dieresis
                case 0x91: case 0xB1: repl = 'n'; break; // N/n
                default: break;
            }
            if (repl != 0) {
                out[j++] = repl;
                i += 2;
                continue;
            }
        }

        if (c == 0xC2 && (c2 == 0xA1 || c2 == 0xBF)) {
            i += 2;
            continue;
        }

        if (c == 0xE2 && c2 == 0x80 && (c3 == 0x93 || c3 == 0x94)) {
            out[j++] = '-';
            i += 3;
            continue;
        }

        i += (c >= 0xE0) ? 3 : 2;
    }
    out[j] = '\0';
}

static void ui_label_set_text_ascii(lv_obj_t *label, const char *text) {
    char clean[256];
    ui_ascii_text(text, clean, sizeof(clean));
    lv_label_set_text(label, clean);
}

#define lv_label_set_text(label, text) ui_label_set_text_ascii(label, text)

static lv_obj_t *g_scr[SCR_COUNT] = {};
static uint32_t  g_activity_ms    = 0;

namespace {

const lv_font_t *F_S = &lv_font_montserrat_12;
const lv_font_t *F_M = &lv_font_montserrat_16;
const lv_font_t *F_L = &lv_font_montserrat_20;

lv_style_t sty_bg, sty_card, sty_btn, sty_btn_pri, sty_title, sty_chip;
lv_color_t C_BG, C_CARD, C_TXT, C_MUT, C_ACC, C_OK, C_WRN, C_ERR, C_BR;

bool     s_dark          = true;
bool     s_theme_rebuild = false;
uint32_t s_tick_prev     = 0;
uint32_t s_splash_ms     = 0;
bool     s_reset_armed   = false;

struct WDash {
    lv_obj_t *mode, *uart, *ph, *phs, *ec, *ecs, *water, *air, *level, *alarm, *action;
} wd;

struct WSens {
    lv_obj_t *ln[9];
} ws;

struct WAct {
    lv_obj_t *sw[10], *wrn;
} wact;

struct WAuto {
    lv_obj_t *mode, *state, *action, *telemetry, *lock;
} wa;

struct WCal {
    lv_obj_t *status, *values, *guide, *resetMsg;
} wcal;

struct WAlm {
    lv_obj_t *status, *history;
} wal;

struct WComm {
    lv_obj_t *wf;
} wcomm;

struct WMaint {
    lv_obj_t *msg;
} wm;

struct WInf {
    lv_obj_t *text;
} wi;

static lv_obj_t *g_stage_card = nullptr;
static lv_obj_t *g_sw_dark    = nullptr;
static lv_obj_t *g_sl_idle    = nullptr;
static lv_obj_t *g_sw_sound   = nullptr;
static lv_obj_t *g_sl_coff    = nullptr;
static lv_obj_t *g_sl_cslope  = nullptr;
static lv_obj_t *g_sl_tdsf    = nullptr;

constexpr const char *kActKey[] = {"PA", "PB", "PHU", "PHD", "REC", "PIN", "LUZ", "INT", "EXT", "BUZ"};
constexpr const char *kActNm[]  = {"Nutr. A", "Nutr. B", "pH+", "pH-", "Recirc.", "Agua IN", "Luz", "Int.", "Ext.", "Buzzer"};
bool s_act_sync = false;

SystemState readState() {
    DataModel::getInstance().lock();
    SystemState st = DataModel::getInstance().getState();
    DataModel::getInstance().unlock();
    return st;
}

SystemSettings readSettings() {
    DataModel::getInstance().lock();
    SystemSettings ss = DataModel::getInstance().getSettings();
    DataModel::getInstance().unlock();
    return ss;
}

void saveSettings(const SystemSettings &ss) {
    DataModel::getInstance().updateSettings(ss);
    settingsSaveFrom(ss);
}

void send_cmd(const char *cmd) {
    UartComm::getInstance().sendCommand(cmd);
}

void palette(bool dark) {
    s_dark = dark;
    if (dark) {
        C_BG   = lv_color_hex(0x0d1117);
        C_CARD = lv_color_hex(0x161b22);
        C_TXT  = lv_color_hex(0xe6edf3);
        C_MUT  = lv_color_hex(0x8b949e);
        C_ACC  = lv_color_hex(0x58a6ff);
        C_OK   = lv_color_hex(0x3fb950);
        C_WRN  = lv_color_hex(0xd29922);
        C_ERR  = lv_color_hex(0xf85149);
        C_BR   = lv_color_hex(0x30363d);
    } else {
        C_BG   = lv_color_hex(0xf0f2f5);
        C_CARD = lv_color_hex(0xffffff);
        C_TXT  = lv_color_hex(0x1f2328);
        C_MUT  = lv_color_hex(0x656d76);
        C_ACC  = lv_color_hex(0x0969da);
        C_OK   = lv_color_hex(0x1a7f37);
        C_WRN  = lv_color_hex(0x9a6700);
        C_ERR  = lv_color_hex(0xcf222e);
        C_BR   = lv_color_hex(0xd0d7de);
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
    lv_style_set_radius(&sty_card, 10);
    lv_style_set_border_color(&sty_card, C_BR);
    lv_style_set_border_width(&sty_card, 1);
    lv_style_set_pad_all(&sty_card, 6);

    lv_style_init(&sty_btn);
    lv_style_set_bg_color(&sty_btn, C_CARD);
    lv_style_set_bg_opa(&sty_btn, LV_OPA_COVER);
    lv_style_set_radius(&sty_btn, 10);
    lv_style_set_border_color(&sty_btn, C_BR);
    lv_style_set_border_width(&sty_btn, 1);
    lv_style_set_pad_all(&sty_btn, 4);
    lv_style_set_text_color(&sty_btn, C_TXT);

    lv_style_init(&sty_btn_pri);
    lv_style_set_bg_color(&sty_btn_pri, C_ACC);
    lv_style_set_bg_opa(&sty_btn_pri, LV_OPA_COVER);
    lv_style_set_radius(&sty_btn_pri, 10);
    lv_style_set_text_color(&sty_btn_pri, s_dark ? lv_color_hex(0x0d1117) : lv_color_white());

    lv_style_init(&sty_title);
    lv_style_set_text_font(&sty_title, F_M);
    lv_style_set_text_color(&sty_title, C_TXT);

    lv_style_init(&sty_chip);
    lv_style_set_bg_color(&sty_chip, C_CARD);
    lv_style_set_bg_opa(&sty_chip, LV_OPA_COVER);
    lv_style_set_radius(&sty_chip, 8);
    lv_style_set_border_color(&sty_chip, C_BR);
    lv_style_set_border_width(&sty_chip, 1);
    lv_style_set_pad_all(&sty_chip, 3);
}

void lblf(lv_obj_t *o, const char *fmt, ...) {
    if (!o || !fmt) return;
    char b[180];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof(b), fmt, a);
    va_end(a);
    lv_label_set_text(o, b);
}

lv_obj_t *mk_screen() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_remove_style_all(s);
    lv_obj_add_style(s, &sty_bg, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s, [](lv_event_t *) { ui_render_bump_activity(); }, LV_EVENT_PRESSED, nullptr);
    return s;
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
    lv_obj_add_style(b, pri ? &sty_btn_pri : &sty_btn, 0);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    return b;
}

lv_obj_t *mk_label(lv_obj_t *p, const char *txt, lv_coord_t x, lv_coord_t y, const lv_font_t *font = nullptr) {
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, txt ? txt : "");
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_text_color(l, C_TXT, 0);
    if (font) lv_obj_set_style_text_font(l, font, 0);
    return l;
}

void nav_cb(lv_event_t *e) {
    ScreenId id = static_cast<ScreenId>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    ui_render_bump_activity();
    UiManager::getInstance().loadScreen(id);
}

void cmd_cb(lv_event_t *e) {
    const char *cmd = static_cast<const char *>(lv_event_get_user_data(e));
    if (cmd) send_cmd(cmd);
    ui_render_bump_activity();
}

void hdr(lv_obj_t *scr, const char *title, ScreenId back) {
    lv_obj_t *t = mk_label(scr, title, 8, 4, F_M);
    lv_obj_set_style_text_color(t, C_TXT, 0);

    if (back < SCR_COUNT) {
        lv_obj_t *bb = mk_btn(scr, 200, 2, 52, 22, false);
        lv_obj_t *lb = lv_label_create(bb);
        lv_label_set_text(lb, "<");
        lv_obj_center(lb);
        lv_obj_add_event_cb(bb, nav_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(back)));

        lv_obj_t *bh = mk_btn(scr, 258, 2, 54, 22, true);
        lv_obj_t *lh = lv_label_create(bh);
        lv_label_set_text(lh, "Inicio");
        lv_obj_center(lh);
        lv_obj_add_event_cb(bh, nav_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(SCR_HOME)));
    }

    lv_obj_t *ln = lv_obj_create(scr);
    lv_obj_remove_style_all(ln);
    lv_obj_set_pos(ln, 0, 27);
    lv_obj_set_size(ln, 320, 2);
    lv_obj_set_style_bg_color(ln, C_ACC, 0);
    lv_obj_set_style_bg_opa(ln, LV_OPA_50, 0);
}

void tile(lv_obj_t *p, lv_coord_t x, lv_coord_t y, const char *txt, ScreenId to, bool pri = false) {
    lv_obj_t *b = mk_btn(p, x, y, 150, 38, pri);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, 136);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, nav_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(to)));
}

const char *modeText(const SystemState &st) {
    if (st.maintenance_mode) return "MANT";
    if (st.auto_mode) return "AUTO";
    return "MANUAL";
}

const char *healthText(const SystemState &st) {
    if (st.health == HEALTH_ALERT) return "ALERTA";
    if (st.health == HEALTH_WARNING) return "AVISO";
    return "OK";
}

lv_color_t healthColor(const SystemState &st) {
    if (st.health == HEALTH_ALERT) return C_ERR;
    if (st.health == HEALTH_WARNING) return C_WRN;
    return C_OK;
}

bool phOk(const SystemState &st) {
    const StageProfile &p = kStageProfiles[clamp_stage(st.active_stage)];
    return st.ph_probe_ok && st.ph >= p.ph_min && st.ph <= p.ph_max;
}

bool tdsOk(const SystemState &st) {
    const StageProfile &p = kStageProfiles[clamp_stage(st.active_stage)];
    return st.tds_probe_ok && st.tds >= p.ppm_min && st.tds <= p.ppm_max;
}

void refreshStageCard() {
    if (!g_stage_card) return;
    SystemState st = readState();
    int i = clamp_stage(st.active_stage);
    const StageProfile &p = kStageProfiles[i];
    lblf(g_stage_card,
         "%s\nObjetivo pH %.2f-%.2f\nPPM %d-%d | Foto %s\nSecuencia A->B 1:1; recirc. y estabilizacion en UNO R4.",
         p.name, p.ph_min, p.ph_max, p.ppm_min, p.ppm_max, p.photoperiod);
}

void setMode(int mode) {
    if (mode == 0) {
        send_cmd("CMD:SET_MAINT:0");
        send_cmd("CMD:SET_AUTO:1");
    } else if (mode == 1) {
        send_cmd("CMD:SET_AUTO:0");
        send_cmd("CMD:SET_MAINT:0");
    } else {
        send_cmd("CMD:SET_AUTO:0");
        send_cmd("CMD:SET_MAINT:1");
    }
}

void build_splash() {
    lv_obj_t *s = mk_screen();
    lv_obj_t *title = mk_label(s, "e-Weed", 0, 52, F_L);
    lv_obj_set_width(title, 320);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(title, C_ACC, 0);

    lv_obj_t *sub = mk_label(s, "HMI hidroponica profesional", 0, 86, F_M);
    lv_obj_set_width(sub, 320);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *hw = mk_label(s, "ESP32 CYD + UNO R4 Minima", 0, 116, F_S);
    lv_obj_set_width(hw, 320);
    lv_obj_set_style_text_align(hw, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(hw, C_MUT, 0);

    lv_obj_t *b = mk_btn(s, 56, 174, 208, 34, true);
    lv_obj_t *lb = lv_label_create(b);
    lv_label_set_text(lb, "Entrar al sistema");
    lv_obj_center(lb);
    lv_obj_add_event_cb(b, nav_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(SCR_DASHBOARD)));
    g_scr[SCR_SPLASH] = s;
}

void build_home() {
    lv_obj_t *s = mk_screen();
    hdr(s, "Inicio", SCR_COUNT);
    tile(s, 8, 36, "Dashboard", SCR_DASHBOARD, true);
    tile(s, 162, 36, "Sensores", SCR_SENSORS);
    tile(s, 8, 80, "Actuadores", SCR_ACTUATORS);
    tile(s, 162, 80, "Automatizacion", SCR_AUTOMATION);
    tile(s, 8, 124, "Calibracion", SCR_CALIBRATION, true);
    tile(s, 162, 124, "Alarmas", SCR_ALARMS);
    tile(s, 8, 168, "Ajustes", SCR_SETTINGS_HUB);
    tile(s, 162, 168, "Mantenimiento", SCR_MAINTENANCE);
    g_scr[SCR_HOME] = s;
}

lv_obj_t *dash_card(lv_obj_t *s, lv_coord_t x, lv_coord_t y, const char *title, lv_obj_t **value, lv_obj_t **state) {
    lv_obj_t *c = mk_card(s, x, y, 150, 48);
    lv_obj_t *h = mk_label(c, title, 2, 0, F_S);
    lv_obj_set_style_text_color(h, C_MUT, 0);
    *value = mk_label(c, "--", 2, 15, F_M);
    *state = mk_label(c, "--", 94, 18, F_S);
    return c;
}

void build_dashboard() {
    lv_obj_t *s = mk_screen();
    hdr(s, "Dashboard", SCR_HOME);

    wd.mode = mk_label(s, "--", 8, 32, F_S);
    wd.uart = mk_label(s, "--", 168, 32, F_S);
    lv_obj_set_width(wd.uart, 144);
    lv_obj_set_style_text_align(wd.uart, LV_TEXT_ALIGN_RIGHT, 0);

    dash_card(s, 8, 50, "pH", &wd.ph, &wd.phs);
    dash_card(s, 162, 50, "EC / TDS", &wd.ec, &wd.ecs);

    lv_obj_t *c1 = mk_card(s, 8, 104, 150, 48);
    mk_label(c1, "Agua", 2, 0, F_S);
    wd.water = mk_label(c1, "--", 2, 17, F_M);

    lv_obj_t *c2 = mk_card(s, 162, 104, 150, 48);
    mk_label(c2, "Ambiente", 2, 0, F_S);
    wd.air = mk_label(c2, "--", 2, 15, F_S);

    wd.level = mk_label(s, "Nivel: --", 8, 158, F_S);
    wd.alarm = mk_label(s, "Alarmas: --", 8, 176, F_S);
    wd.action = mk_label(s, "Ultima accion: --", 8, 198, F_S);
    lv_label_set_long_mode(wd.action, LV_LABEL_LONG_DOT);
    lv_obj_set_width(wd.action, 304);
    g_scr[SCR_DASHBOARD] = s;
}

void build_stages() {
    lv_obj_t *s = mk_screen();
    hdr(s, "Etapas", SCR_HOME);
    for (int i = 0; i < 5; ++i) {
        lv_obj_t *b = mk_btn(s, 8, 36 + i * 30, 112, 24, i == 0);
        lv_obj_t *l = lv_label_create(b);
        lblf(l, "Etapa %d", i + 1);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, [](lv_event_t *e) {
            int ix = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
            char buf[28];
            snprintf(buf, sizeof(buf), "CMD:SET_STAGE:%d", ix);
            send_cmd(buf);
            ui_render_bump_activity();
            refreshStageCard();
        }, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    }
    lv_obj_t *c = mk_card(s, 128, 36, 184, 168);
    g_stage_card = mk_label(c, "--", 2, 2, F_S);
    lv_label_set_long_mode(g_stage_card, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_stage_card, 174);
    refreshStageCard();
    g_scr[SCR_STAGES] = s;
}

void build_sensors() {
    lv_obj_t *s = mk_screen();
    hdr(s, "Sensores", SCR_HOME);
    for (int i = 0; i < 9; ++i) {
        ws.ln[i] = mk_label(s, "--", 8, 34 + i * 20, F_S);
        lv_label_set_long_mode(ws.ln[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(ws.ln[i], 304);
    }
    g_scr[SCR_SENSORS] = s;
}

void act_sw(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || s_act_sync) return;
    int i = static_cast<int>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    SystemState st = readState();
    if (st.auto_mode && !st.maintenance_mode) return;
    char b[40];
    snprintf(b, sizeof(b), "CMD:OUT:%s:%d", kActKey[i], on ? 1 : 0);
    send_cmd(b);
}

void build_actuators() {
    lv_obj_t *s = mk_screen();
    hdr(s, "Actuadores", SCR_HOME);
    wact.wrn = mk_label(s, "AUTO: salidas bloqueadas. Use MAN/MANT.", 8, 30, F_S);
    lv_obj_set_style_text_color(wact.wrn, C_WRN, 0);

    mk_label(s, "12V", 8, 48, F_S);
    for (int i = 0; i < 6; ++i) {
        lv_obj_t *row = mk_card(s, 48, 48 + i * 20, 264, 18);
        mk_label(row, kActNm[i], 2, -1, F_S);
        wact.sw[i] = lv_switch_create(row);
        lv_obj_set_size(wact.sw[i], 42, 18);
        lv_obj_set_pos(wact.sw[i], 210, -2);
        lv_obj_add_event_cb(wact.sw[i], act_sw, LV_EVENT_VALUE_CHANGED, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    }

    mk_label(s, "220V", 8, 174, F_S);
    for (int i = 6; i < 9; ++i) {
        lv_obj_t *row = mk_card(s, 58 + (i - 6) * 84, 172, 78, 36);
        lv_obj_t *lb = mk_label(row, kActNm[i], 2, 0, F_S);
        lv_obj_set_width(lb, 42);
        wact.sw[i] = lv_switch_create(row);
        lv_obj_set_size(wact.sw[i], 34, 18);
        lv_obj_set_pos(wact.sw[i], 36, 10);
        lv_obj_add_event_cb(wact.sw[i], act_sw, LV_EVENT_VALUE_CHANGED, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    }

    wact.sw[9] = nullptr;
    g_scr[SCR_ACTUATORS] = s;
}

void build_automation() {
    lv_obj_t *s = mk_screen();
    hdr(s, "Automatizacion", SCR_HOME);

    lv_obj_t *b0 = mk_btn(s, 8, 34, 96, 28, false);
    lv_label_set_text(lv_label_create(b0), "Manual");
    lv_obj_center(lv_obj_get_child(b0, 0));
    lv_obj_add_event_cb(b0, [](lv_event_t *) { setMode(1); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *b1 = mk_btn(s, 112, 34, 96, 28, true);
    lv_label_set_text(lv_label_create(b1), "Auto");
    lv_obj_center(lv_obj_get_child(b1, 0));
    lv_obj_add_event_cb(b1, [](lv_event_t *) { setMode(0); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *b2 = mk_btn(s, 216, 34, 96, 28, false);
    lv_label_set_text(lv_label_create(b2), "Mant.");
    lv_obj_center(lv_obj_get_child(b2, 0));
    lv_obj_add_event_cb(b2, [](lv_event_t *) { setMode(2); }, LV_EVENT_CLICKED, nullptr);

    wa.mode = mk_label(s, "Modo: --", 8, 72, F_S);
    wa.state = mk_label(s, "Controlador: --", 8, 92, F_S);
    wa.action = mk_label(s, "Paso: --", 8, 112, F_S);
    wa.telemetry = mk_label(s, "Telemetria: --", 8, 132, F_S);
    wa.lock = mk_label(s, "Interlocks: --", 8, 154, F_S);
    lv_label_set_long_mode(wa.lock, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(wa.lock, 304);
    g_scr[SCR_AUTOMATION] = s;
}

void build_calibration() {
    lv_obj_t *s = mk_screen();
    hdr(s, "Calibracion guiada", SCR_HOME);

    lv_obj_t *tv = lv_tabview_create(s, LV_DIR_TOP, 24);
    lv_obj_set_pos(tv, 0, 28);
    lv_obj_set_size(tv, 320, 210);

    lv_obj_t *t1 = lv_tabview_add_tab(tv, "pH");
    lv_obj_t *t2 = lv_tabview_add_tab(tv, "TDS");
    lv_obj_t *t3 = lv_tabview_add_tab(tv, "Valores");
    lv_obj_t *t4 = lv_tabview_add_tab(tv, "Reset");

    lv_obj_t *phInfo = mk_label(t1,
        "Asistente pH UNO R4\n1) Modo mantenimiento\n2) Sonda en buffer 4.00\n3) Esperar estabilidad\n4) Repetir con 7.00",
        8, 6, F_S);
    lv_label_set_long_mode(phInfo, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(phInfo, 286);

    lv_obj_t *bm = mk_btn(t1, 8, 92, 138, 28, false);
    lv_label_set_text(lv_label_create(bm), "Modo mant.");
    lv_obj_center(lv_obj_get_child(bm, 0));
    lv_obj_add_event_cb(bm, cmd_cb, LV_EVENT_CLICKED, (void *)"CMD:SET_MAINT:1");

    lv_obj_t *bs = mk_btn(t1, 154, 92, 138, 28, true);
    lv_label_set_text(lv_label_create(bs), "Iniciar pH");
    lv_obj_center(lv_obj_get_child(bs, 0));
    lv_obj_add_event_cb(bs, cmd_cb, LV_EVENT_CLICKED, (void *)"CMD:CAL_PH_START");

    mk_label(t1, "Offset", 8, 130, F_S);
    g_sl_coff = lv_slider_create(t1);
    lv_slider_set_range(g_sl_coff, -3000, 3500);
    lv_obj_set_pos(g_sl_coff, 68, 134);
    lv_obj_set_size(g_sl_coff, 222, 10);

    mk_label(t1, "Pend.", 8, 154, F_S);
    g_sl_cslope = lv_slider_create(t1);
    lv_slider_set_range(g_sl_cslope, -1000, 500);
    lv_obj_set_pos(g_sl_cslope, 68, 158);
    lv_obj_set_size(g_sl_cslope, 222, 10);

    lv_obj_t *bphSave = mk_btn(t1, 8, 176, 284, 28, true);
    lv_label_set_text(lv_label_create(bphSave), "Guardar calibracion pH");
    lv_obj_center(lv_obj_get_child(bphSave, 0));
    lv_obj_add_event_cb(bphSave, [](lv_event_t *) {
        SystemSettings ss = readSettings();
        ss.ph_offset = static_cast<float>(lv_slider_get_value(g_sl_coff)) / 100.0f;
        ss.ph_slope  = static_cast<float>(lv_slider_get_value(g_sl_cslope)) / 100.0f;
        ss.calibration_dirty = true;
        saveSettings(ss);
        char buf[56];
        snprintf(buf, sizeof(buf), "CMD:CAL_PH_SAVE:%.3f:%.3f", ss.ph_offset, ss.ph_slope);
        send_cmd(buf);
        if (wcal.status) lv_label_set_text(wcal.status, "pH guardado y enviado a UNO R4");
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *tdsInfo = mk_label(t2,
        "Calibracion TDS/EC\nUse solucion patron. Ajuste el factor solo si la lectura estable difiere del patron.",
        8, 8, F_S);
    lv_label_set_long_mode(tdsInfo, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(tdsInfo, 286);

    lv_obj_t *bt = mk_btn(t2, 8, 64, 138, 30, true);
    lv_label_set_text(lv_label_create(bt), "Iniciar TDS");
    lv_obj_center(lv_obj_get_child(bt, 0));
    lv_obj_add_event_cb(bt, cmd_cb, LV_EVENT_CLICKED, (void *)"CMD:CAL_TDS_START");

    mk_label(t2, "Factor x0.01", 8, 108, F_S);
    g_sl_tdsf = lv_slider_create(t2);
    lv_slider_set_range(g_sl_tdsf, 50, 400);
    lv_obj_set_pos(g_sl_tdsf, 8, 130);
    lv_obj_set_size(g_sl_tdsf, 284, 10);

    lv_obj_t *btdsSave = mk_btn(t2, 8, 164, 284, 30, true);
    lv_label_set_text(lv_label_create(btdsSave), "Guardar factor TDS");
    lv_obj_center(lv_obj_get_child(btdsSave, 0));
    lv_obj_add_event_cb(btdsSave, [](lv_event_t *) {
        SystemSettings ss = readSettings();
        ss.tds_cal_factor = static_cast<float>(lv_slider_get_value(g_sl_tdsf)) / 100.0f;
        ss.calibration_dirty = true;
        saveSettings(ss);
        char buf[44];
        snprintf(buf, sizeof(buf), "CMD:CAL_TDS_SAVE:%.4f", ss.tds_cal_factor);
        send_cmd(buf);
        if (wcal.status) lv_label_set_text(wcal.status, "TDS guardado y enviado a UNO R4");
    }, LV_EVENT_CLICKED, nullptr);

    wcal.values = mk_label(t3, "--", 8, 8, F_S);
    lv_label_set_long_mode(wcal.values, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(wcal.values, 286);
    wcal.status = mk_label(t3, "Listo para calibrar.", 8, 118, F_S);
    lv_obj_set_style_text_color(wcal.status, C_ACC, 0);

    wcal.resetMsg = mk_label(t4,
        "Reset seguro:\nPrimer toque arma la restauracion.\nSegundo toque confirma.",
        8, 8, F_S);
    lv_label_set_long_mode(wcal.resetMsg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(wcal.resetMsg, 286);

    lv_obj_t *br = mk_btn(t4, 8, 100, 284, 34, true);
    lv_label_set_text(lv_label_create(br), "Restaurar fabrica");
    lv_obj_center(lv_obj_get_child(br, 0));
    lv_obj_add_event_cb(br, [](lv_event_t *) {
        if (!s_reset_armed) {
            s_reset_armed = true;
            if (wcal.resetMsg) lv_label_set_text(wcal.resetMsg, "Reset armado. Pulse nuevamente para confirmar.");
            return;
        }
        s_reset_armed = false;
        SystemSettings ss = readSettings();
        ss.ph_offset = 21.340f;
        ss.ph_slope = -5.700f;
        ss.tds_cal_factor = 1.0000f;
        ss.calibration_dirty = true;
        saveSettings(ss);
        send_cmd("CMD:CAL_PH_SAVE:21.340:-5.700");
        send_cmd("CMD:CAL_TDS_SAVE:1.0000");
        if (wcal.resetMsg) lv_label_set_text(wcal.resetMsg, "Calibracion restaurada y enviada a UNO R4.");
    }, LV_EVENT_CLICKED, nullptr);

    g_scr[SCR_CALIBRATION] = s;
}

void build_alarms() {
    lv_obj_t *s = mk_screen();
    hdr(s, "Alarmas", SCR_HOME);
    wal.status = mk_label(s, "--", 8, 38, F_M);
    wal.history = mk_label(s, "Historial: --", 8, 72, F_S);
    lv_label_set_long_mode(wal.history, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(wal.history, 304);
    lv_obj_t *ack = mk_btn(s, 8, 190, 144, 30, true);
    lv_label_set_text(lv_label_create(ack), "ACK / Reset");
    lv_obj_center(lv_obj_get_child(ack, 0));
    lv_obj_add_event_cb(ack, cmd_cb, LV_EVENT_CLICKED, (void *)"CMD:ACK_ALARM");
    g_scr[SCR_ALARMS] = s;
}

void hub_item(lv_obj_t *p, lv_coord_t y, const char *txt, ScreenId to) {
    lv_obj_t *b = mk_btn(p, 8, y, 304, 23, false);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_add_event_cb(b, nav_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(to)));
}

void build_settings_hub() {
    lv_obj_t *s = mk_screen();
    hdr(s, "Ajustes", SCR_HOME);
    int y = 34;
    hub_item(s, y, "Objetivos por etapa", SCR_SET_STAGE); y += 25;
    hub_item(s, y, "Limites de seguridad", SCR_SET_SAFETY); y += 25;
    hub_item(s, y, "Recirculacion", SCR_SET_RECIRC); y += 25;
    hub_item(s, y, "Estabilizacion", SCR_SET_STAB); y += 25;
    hub_item(s, y, "Microdosis", SCR_SET_MICRO); y += 25;
    hub_item(s, y, "Fotoperiodo", SCR_SET_PHOTO); y += 25;
    hub_item(s, y, "Ventilacion", SCR_SET_VENT); y += 25;
    hub_item(s, y, "Comunicacion / WiFi", SCR_SET_COMM); y += 25;
    hub_item(s, y, "Pantalla y sonido", SCR_SET_DISPLAY);
    g_scr[SCR_SETTINGS_HUB] = s;
}

void build_simple_settings(const char *title, const char *body, ScreenId id) {
    lv_obj_t *s = mk_screen();
    hdr(s, title, SCR_SETTINGS_HUB);
    lv_obj_t *l = mk_label(s, body, 8, 38, F_S);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, 304);
    g_scr[id] = s;
}

void build_set_stage() {
    lv_obj_t *s = mk_screen();
    hdr(s, "Objetivos por etapa", SCR_SETTINGS_HUB);
    mk_label(s, "Ajuste de perfiles desde UNO R4 o firmware. Vista HMI:", 8, 36, F_S);
    g_stage_card = mk_label(s, "--", 8, 64, F_S);
    lv_label_set_long_mode(g_stage_card, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_stage_card, 304);
    refreshStageCard();
    g_scr[SCR_SET_STAGE] = s;
}

void build_set_comm() {
    lv_obj_t *s = mk_screen();
    hdr(s, "Comunicacion", SCR_SETTINGS_HUB);
    lv_obj_t *l = mk_label(s, "UART2 hacia UNO R4: 115200 baudios. WiFi AP/STA local para API.", 8, 36, F_S);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, 304);
    lv_obj_t *b = mk_btn(s, 8, 100, 304, 30, true);
    lv_label_set_text(lv_label_create(b), "Guardar baud 115200");
    lv_obj_center(lv_obj_get_child(b, 0));
    lv_obj_add_event_cb(b, [](lv_event_t *) {
        SystemSettings ss = readSettings();
        ss.uart_baud = 115200;
        saveSettings(ss);
    }, LV_EVENT_CLICKED, nullptr);
    wcomm.wf = mk_label(s, "WiFi: --", 8, 150, F_S);
    lv_label_set_long_mode(wcomm.wf, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(wcomm.wf, 304);
    g_scr[SCR_SET_COMM] = s;
}

void build_set_display() {
    lv_obj_t *s = mk_screen();
    hdr(s, "Pantalla", SCR_SETTINGS_HUB);
    mk_label(s, "Tema oscuro", 8, 40, F_S);
    g_sw_dark = lv_switch_create(s);
    lv_obj_set_pos(g_sw_dark, 210, 36);
    mk_label(s, "Reposo (seg)", 8, 78, F_S);
    g_sl_idle = lv_slider_create(s);
    lv_slider_set_range(g_sl_idle, 20, 600);
    lv_obj_set_pos(g_sl_idle, 8, 104);
    lv_obj_set_size(g_sl_idle, 286, 12);
    lv_obj_t *b = mk_btn(s, 8, 176, 304, 30, true);
    lv_label_set_text(lv_label_create(b), "Guardar pantalla");
    lv_obj_center(lv_obj_get_child(b, 0));
    lv_obj_add_event_cb(b, [](lv_event_t *) {
        SystemSettings ss = readSettings();
        ss.dark_theme = lv_obj_has_state(g_sw_dark, LV_STATE_CHECKED);
        ss.ui_idle_timeout_ms = static_cast<uint32_t>(lv_slider_get_value(g_sl_idle)) * 1000UL;
        saveSettings(ss);
        s_theme_rebuild = true;
    }, LV_EVENT_CLICKED, nullptr);
    g_scr[SCR_SET_DISPLAY] = s;
}

void build_set_sound() {
    lv_obj_t *s = mk_screen();
    hdr(s, "Sonido", SCR_SETTINGS_HUB);
    mk_label(s, "Alertas habilitadas", 8, 44, F_S);
    g_sw_sound = lv_switch_create(s);
    lv_obj_set_pos(g_sw_sound, 210, 40);
    lv_obj_t *b = mk_btn(s, 8, 176, 304, 30, true);
    lv_label_set_text(lv_label_create(b), "Guardar sonido");
    lv_obj_center(lv_obj_get_child(b, 0));
    lv_obj_add_event_cb(b, [](lv_event_t *) {
        SystemSettings ss = readSettings();
        ss.sound_enabled = lv_obj_has_state(g_sw_sound, LV_STATE_CHECKED);
        saveSettings(ss);
    }, LV_EVENT_CLICKED, nullptr);
    g_scr[SCR_SET_SOUND] = s;
}

void build_maintenance() {
    lv_obj_t *s = mk_screen();
    hdr(s, "Mantenimiento", SCR_HOME);
    wm.msg = mk_label(s, "Pruebas rapidas del sistema.", 8, 196, F_S);

    lv_obj_t *b1 = mk_btn(s, 8, 38, 304, 28, false);
    lv_label_set_text(lv_label_create(b1), "Solicitar estado UNO R4");
    lv_obj_center(lv_obj_get_child(b1, 0));
    lv_obj_add_event_cb(b1, [](lv_event_t *) {
        send_cmd("CMD:GET_STATUS");
        if (wm.msg) lv_label_set_text(wm.msg, "GET_STATUS enviado a UNO R4");
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *b2 = mk_btn(s, 8, 74, 304, 28, false);
    lv_label_set_text(lv_label_create(b2), "Ver sensores");
    lv_obj_center(lv_obj_get_child(b2, 0));
    lv_obj_add_event_cb(b2, nav_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(SCR_SENSORS)));

    lv_obj_t *b3 = mk_btn(s, 8, 110, 304, 28, false);
    lv_label_set_text(lv_label_create(b3), "Ver actuadores");
    lv_obj_center(lv_obj_get_child(b3, 0));
    lv_obj_add_event_cb(b3, nav_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(SCR_ACTUATORS)));

    lv_obj_t *b4 = mk_btn(s, 8, 146, 304, 28, true);
    lv_label_set_text(lv_label_create(b4), "Info firmware");
    lv_obj_center(lv_obj_get_child(b4, 0));
    lv_obj_add_event_cb(b4, nav_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(SCR_SYSTEM_INFO)));

    g_scr[SCR_MAINTENANCE] = s;
}

void build_info() {
    lv_obj_t *s = mk_screen();
    hdr(s, "Sistema", SCR_HOME);
    wi.text = mk_label(s, "--", 8, 38, F_S);
    lv_label_set_long_mode(wi.text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(wi.text, 304);
    g_scr[SCR_SYSTEM_INFO] = s;
}

void do_build(ScreenId id) {
    switch (id) {
        case SCR_SPLASH: build_splash(); break;
        case SCR_HOME: build_home(); break;
        case SCR_DASHBOARD: build_dashboard(); break;
        case SCR_STAGES: build_stages(); break;
        case SCR_SENSORS: build_sensors(); break;
        case SCR_ACTUATORS: build_actuators(); break;
        case SCR_AUTOMATION: build_automation(); break;
        case SCR_CALIBRATION: build_calibration(); break;
        case SCR_ALARMS: build_alarms(); break;
        case SCR_SETTINGS_HUB: build_settings_hub(); break;
        case SCR_SET_STAGE: build_set_stage(); break;
        case SCR_SET_SAFETY: build_simple_settings("Seguridad", "Limites locales de visualizacion y alarma. El control fisico se ejecuta en UNO R4.", id); break;
        case SCR_SET_RECIRC: build_simple_settings("Recirculacion", "Ajustes de ciclos de recirculacion. Mantener cambios coordinados con UNO R4.", id); break;
        case SCR_SET_STAB: build_simple_settings("Estabilizacion", "Tiempo de espera entre dosis y nueva medicion para evitar sobredosificacion.", id); break;
        case SCR_SET_MICRO: build_simple_settings("Microdosis", "Volumen base de dosificacion. Ajustar con pruebas reales de bomba peristaltica.", id); break;
        case SCR_SET_PHOTO: build_simple_settings("Fotoperiodo", "Horario de luz. Las cargas 220V deben estar aisladas y protegidas.", id); break;
        case SCR_SET_VENT: build_simple_settings("Ventilacion", "Umbrales de ventilacion por temperatura/humedad. Control fisico en UNO R4.", id); break;
        case SCR_SET_COMM: build_set_comm(); break;
        case SCR_SET_DISPLAY: build_set_display(); break;
        case SCR_SET_SOUND: build_set_sound(); break;
        case SCR_MAINTENANCE: build_maintenance(); break;
        case SCR_SYSTEM_INFO: build_info(); break;
        default: build_home(); break;
    }
}

void syncSettings() {
    SystemSettings ss = readSettings();
    if (g_sw_dark) {
        if (ss.dark_theme) lv_obj_add_state(g_sw_dark, LV_STATE_CHECKED);
        else lv_obj_clear_state(g_sw_dark, LV_STATE_CHECKED);
    }
    if (g_sl_idle) {
        uint32_t sec = ss.ui_idle_timeout_ms / 1000UL;
        if (sec < 20) sec = 20;
        if (sec > 600) sec = 600;
        lv_slider_set_value(g_sl_idle, static_cast<int>(sec), LV_ANIM_OFF);
    }
    if (g_sw_sound) {
        if (ss.sound_enabled) lv_obj_add_state(g_sw_sound, LV_STATE_CHECKED);
        else lv_obj_clear_state(g_sw_sound, LV_STATE_CHECKED);
    }
    if (g_sl_coff && g_sl_cslope) {
        lv_slider_set_value(g_sl_coff, static_cast<int>(ss.ph_offset * 100.0f), LV_ANIM_OFF);
        lv_slider_set_value(g_sl_cslope, static_cast<int>(ss.ph_slope * 100.0f), LV_ANIM_OFF);
    }
    if (g_sl_tdsf) {
        int v = static_cast<int>(ss.tds_cal_factor * 100.0f);
        if (v < 50) v = 50;
        if (v > 400) v = 400;
        lv_slider_set_value(g_sl_tdsf, v, LV_ANIM_OFF);
    }
}

void tick_dashboard(const SystemState &st, uint32_t now) {
    if (!wd.mode) return;
    lblf(wd.mode, "Modo %s | Salud %s", modeText(st), healthText(st));
    lv_obj_set_style_text_color(wd.mode, healthColor(st), 0);
    lblf(wd.uart, "UART %s | %lus", st.telemetry_live ? "OK" : "OFF", (unsigned long)((now - st.uart_last_rx_ms) / 1000UL));

    if (st.telemetry_live && st.ph_probe_ok) lblf(wd.ph, "%.2f", st.ph);
    else lv_label_set_text(wd.ph, "--");
    lv_label_set_text(wd.phs, phOk(st) ? "OK" : "REV");
    lv_obj_set_style_text_color(wd.phs, phOk(st) ? C_OK : C_WRN, 0);

    if (st.telemetry_live && st.tds_probe_ok) lblf(wd.ec, "%d ppm", st.tds);
    else lv_label_set_text(wd.ec, "--");
    lv_label_set_text(wd.ecs, tdsOk(st) ? "OK" : "REV");
    lv_obj_set_style_text_color(wd.ecs, tdsOk(st) ? C_OK : C_WRN, 0);

    lblf(wd.water, "%.1f C", st.temp_water);
    lblf(wd.air, "%.1f C / %.0f%%", st.temp_air, st.hum_air);
    lblf(wd.level, "Nivel: %s  min=%s max=%s", st.level_min ? "BAJO" : (st.level_max ? "ALTO" : "OK"), st.level_min ? "ON" : "ok", st.level_max ? "ON" : "ok");
    lblf(wd.alarm, st.current_alarm ? "Alarma: %s" : "Alarmas: sin alarmas", st.alarm_message);
    lv_obj_set_style_text_color(wd.alarm, st.current_alarm ? C_ERR : C_OK, 0);
    lblf(wd.action, "Ultima accion: %s", st.last_action);
}

void tick_sensors(const SystemState &st, uint32_t now) {
    if (!ws.ln[0]) return;
    lblf(ws.ln[0], "pH: %s %.2f", st.ph_probe_ok ? "OK" : "invalido", st.ph);
    lblf(ws.ln[1], "TDS/EC: %s %d ppm | %.2f mS", st.tds_probe_ok ? "OK" : "invalido", st.tds, st.tds / 500.0f);
    lblf(ws.ln[2], "T agua: %.1f C | sonda %s", st.temp_water, st.tw_probe_ok ? "OK" : "invalida");
    lblf(ws.ln[3], "T aire: %.1f C | DHT %s", st.temp_air, st.dht_online ? "OK" : "OFF");
    lblf(ws.ln[4], "Humedad: %.0f %%", st.hum_air);
    lblf(ws.ln[5], "Nivel min:%s max:%s", st.level_min ? "ON" : "ok", st.level_max ? "ON" : "ok");
    lblf(ws.ln[6], "RTC UNO R4: %s | Hora: %s", st.rtc_online ? "OK" : "no OK", st.controller_clock[0] ? st.controller_clock : "--");
    lblf(ws.ln[7], "Telemetria: %s | hace %lus", st.telemetry_live ? "viva" : "sin datos", (unsigned long)((now - st.uart_last_rx_ms) / 1000UL));
    lblf(ws.ln[8], "Paquetes UART OK:%lu ERR:%lu", (unsigned long)st.uart_ok_packets, (unsigned long)st.uart_bad_packets);
}

void tick_actuators(const SystemState &st) {
    if (!wact.wrn) return;
    bool disabled = st.auto_mode && !st.maintenance_mode;
    lv_label_set_text(wact.wrn, disabled ? "AUTO activo: conmutacion deshabilitada." : "MAN/MANT: conmutacion habilitada.");
    lv_obj_set_style_text_color(wact.wrn, disabled ? C_WRN : C_OK, 0);
    const bool vals[] = {st.state_pump_a, st.state_pump_b, st.state_ph_up, st.state_ph_down, st.state_recirculation, st.state_pump_in, st.state_light, st.state_intractor, st.state_extractor, st.state_buzzer};
    s_act_sync = true;
    for (int i = 0; i < 10; ++i) {
        if (!wact.sw[i]) continue;
        if (vals[i]) lv_obj_add_state(wact.sw[i], LV_STATE_CHECKED);
        else lv_obj_clear_state(wact.sw[i], LV_STATE_CHECKED);
        if (disabled) lv_obj_add_state(wact.sw[i], LV_STATE_DISABLED);
        else lv_obj_clear_state(wact.sw[i], LV_STATE_DISABLED);
    }
    s_act_sync = false;
}

void tick_auto(const SystemState &st, uint32_t now) {
    if (!wa.mode) return;
    lblf(wa.mode, "Modo: %s", modeText(st));
    lblf(wa.state, "Controlador UNO R4: %s", st.telemetry_live ? "conectado" : "sin enlace");
    lblf(wa.action, "Paso actual: %s", st.last_action);
    lblf(wa.telemetry, "Telemetria hace %lus", (unsigned long)((now - st.uart_last_rx_ms) / 1000UL));
    if (st.auto_mode && !st.maintenance_mode) lv_label_set_text(wa.lock, "AUTO: la HMI no fuerza salidas. Interlocks y reles quedan bajo UNO R4.");
    else if (st.maintenance_mode) lv_label_set_text(wa.lock, "MANT: control manual permitido con supervision.");
    else lv_label_set_text(wa.lock, "MANUAL: automatizacion detenida.");
}

void tick_cal(const SystemSettings &ss, const SystemState &st) {
    if (!wcal.values) return;
    lblf(wcal.values,
         "Lectura actual\npH %.2f | TDS %d ppm\n\nCalibracion guardada\npH offset %.3f\npH pendiente %.3f\nTDS factor %.4f\nRefs pH %.2f / %.2f",
         st.ph, st.tds, ss.ph_offset, ss.ph_slope, ss.tds_cal_factor, ss.ph_cal_ref1, ss.ph_cal_ref2);
}

void tick_alarms(const SystemState &st) {
    if (!wal.status) return;
    if (st.current_alarm) {
        lblf(wal.status, "ACTIVA: %s", st.alarm_message);
        lv_obj_set_style_text_color(wal.status, C_ERR, 0);
    } else {
        lv_label_set_text(wal.status, "Sin alarmas activas.");
        lv_obj_set_style_text_color(wal.status, C_OK, 0);
    }
    char buf[240] = "Historial:\n";
    size_t pos = strlen(buf);
    for (int i = 0; i < st.alarm_history_count && i < SystemState::kAlarmHist; ++i) {
        int n = snprintf(buf + pos, sizeof(buf) - pos, "- %s\n", st.alarm_history[i]);
        if (n <= 0) break;
        pos += static_cast<size_t>(n);
        if (pos >= sizeof(buf)) break;
    }
    lv_label_set_text(wal.history, buf);
}

void tick_comm(const SystemState &st) {
    if (!wcomm.wf) return;
    lblf(wcomm.wf, "WiFi: %s\nModo: %s\nIP: %s\nRSSI: %ld\nUART OK:%lu ERR:%lu",
         st.wifi_connected ? "conectado" : "offline",
         st.wifi_ap_mode ? "AP" : "STA",
         st.wifi_ip[0] ? st.wifi_ip : "--",
         static_cast<long>(st.wifi_rssi),
         static_cast<unsigned long>(st.uart_ok_packets),
         static_cast<unsigned long>(st.uart_bad_packets));
}

void tick_info(const SystemState &st) {
    if (!wi.text) return;
    lblf(wi.text,
         "HMI %s\nProtocolo UART %s\nAssets %s\nControlador: Arduino UNO R4 Minima\nUptime %lus\nHeap %u\nLittleFS %s\nUART OK:%lu ERR:%lu",
         EWEED_HMI_VERSION, EWEED_UART_PROTOCOL_VERSION, EWEED_ASSETS_BUNDLE_REV,
         static_cast<unsigned long>(millis() / 1000UL), static_cast<unsigned>(ESP.getFreeHeap()),
         assets_fs_ready() ? "montado" : "error",
         static_cast<unsigned long>(st.uart_ok_packets), static_cast<unsigned long>(st.uart_bad_packets));
}

}  // namespace

void ui_render_bump_activity() { g_activity_ms = millis(); }

lv_obj_t *ui_render_screen_ptr(ScreenId id) {
    if (id >= SCR_COUNT) return nullptr;
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
    memset(&wact, 0, sizeof(wact));
    memset(&wa, 0, sizeof(wa));
    memset(&wcal, 0, sizeof(wcal));
    memset(&wal, 0, sizeof(wal));
    memset(&wcomm, 0, sizeof(wcomm));
    memset(&wm, 0, sizeof(wm));
    memset(&wi, 0, sizeof(wi));
    g_stage_card = nullptr;
    g_sw_dark = nullptr;
    g_sl_idle = nullptr;
    g_sw_sound = nullptr;
    g_sl_coff = nullptr;
    g_sl_cslope = nullptr;
    g_sl_tdsf = nullptr;
    s_reset_armed = false;
}

void ui_render_init() {
    SystemSettings ss = readSettings();
    s_dark = ss.dark_theme;
    styles_init();
    ui_render_destroy_all();
    s_tick_prev = millis();
    g_activity_ms = millis();
}

void ui_render_rebuild_theme() {
    ScreenId resume = UiManager::getInstance().currentScreen;
    ui_render_init();
    ui_render_ensure(resume);
    if (g_scr[resume]) lv_scr_load(g_scr[resume]);
}

void ui_render_ensure(ScreenId id) {
    if (id >= SCR_COUNT) return;
    if (!g_scr[id]) {
        do_build(id);
        syncSettings();
    }
}

void ui_render_tick(uint32_t now_ms) {
    if (s_theme_rebuild) {
        s_theme_rebuild = false;
        ui_render_rebuild_theme();
        return;
    }

    if (now_ms - s_tick_prev < 400) return;
    s_tick_prev = now_ms;

    ScreenId current = UiManager::getInstance().currentScreen;
    if (current >= SCR_COUNT) return;

    SystemState st = readState();
    SystemSettings ss = readSettings();

    switch (current) {
        case SCR_SPLASH:
            if (s_splash_ms == 0) s_splash_ms = now_ms;
            break;
        case SCR_DASHBOARD:
            tick_dashboard(st, now_ms);
            break;
        case SCR_STAGES:
        case SCR_SET_STAGE:
            refreshStageCard();
            break;
        case SCR_SENSORS:
            tick_sensors(st, now_ms);
            break;
        case SCR_ACTUATORS:
            tick_actuators(st);
            break;
        case SCR_AUTOMATION:
            tick_auto(st, now_ms);
            break;
        case SCR_CALIBRATION:
            tick_cal(ss, st);
            break;
        case SCR_ALARMS:
            tick_alarms(st);
            break;
        case SCR_SET_COMM:
            tick_comm(st);
            break;
        case SCR_SYSTEM_INFO:
            tick_info(st);
            break;
        default:
            break;
    }
}
