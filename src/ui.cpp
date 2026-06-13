// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Alexandre Richard -- https://github.com/Alex77138/Energy-Dashboard
#include "ui.h"
#include "device_config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>

extern bool ap_active;

// ─── Palette ──────────────────────────────────────────────────────────────────
#define C_BG     lv_color_hex(0x0D1117)
#define C_CARD   lv_color_hex(0x161B22)
#define C_SOLAR  lv_color_hex(0xF4A429)
#define C_GRID   lv_color_hex(0x58A6FF)
#define C_GREEN  lv_color_hex(0x3FB950)
#define C_DANGER lv_color_hex(0xF85149)
#define C_TEXT   lv_color_hex(0xE6EDF3)
#define C_MUTED  lv_color_hex(0x8B949E)
#define C_BORDER lv_color_hex(0x30363D)

// ─── Layout ───────────────────────────────────────────────────────────────────
static constexpr int MARGIN   = 8;
static constexpr int GAP      = 8;
static constexpr int BOT_H    = 22;
static constexpr int TOTAL_H  = 480 - MARGIN - BOT_H;
static constexpr int DEV_H    = 168;
static constexpr int MAIN_H   = TOTAL_H - GAP - DEV_H;
static constexpr int CARD_W_2 = (800 - 2 * MARGIN - GAP) / 2;

// ─── Écrans ───────────────────────────────────────────────────────────────────
static lv_obj_t *scr_main   = nullptr;
static lv_obj_t *scr_solar  = nullptr;
static lv_obj_t *scr_grid   = nullptr;
static lv_obj_t *scr_bat    = nullptr;
static lv_obj_t *scr_router = nullptr;

// ─── Navigation ───────────────────────────────────────────────────────────────
static volatile int s_nav_request = -1;
static uint32_t nav_last_ms = 0;

// ─── Widgets écran principal ──────────────────────────────────────────────────
static lv_obj_t *lbl_grid_status, *lbl_grid_power, *lbl_grid_kwh;
static lv_obj_t *lbl_solar_status, *lbl_solar_power, *lbl_solar_kwh;
static lv_obj_t *lbl_solar_dc, *lbl_solar_limit;
static lv_obj_t *lbl_autoconso     = nullptr;
static lv_obj_t *solar_bar         = nullptr;
static lv_obj_t *solar_bar_pct     = nullptr;
static lv_obj_t *ind_strip         = nullptr;
static lv_obj_t *lbl_ip_bar;

static lv_obj_t *lbl_bat_power[MAX_BATTERIES]    = {};
static lv_obj_t *lbl_bat_soc[MAX_BATTERIES]      = {};
static lv_obj_t *lbl_bat_status[MAX_BATTERIES]   = {};
static lv_obj_t *lbl_rtr_power[MAX_ROUTERS]      = {};
static lv_obj_t *lbl_rtr_duration[MAX_ROUTERS]   = {};
static lv_obj_t *lbl_rtr_kwh[MAX_ROUTERS]        = {};
static lv_obj_t *lbl_rtr_status[MAX_ROUTERS]     = {};

// ─── Widgets écran solaire ────────────────────────────────────────────────────
static lv_obj_t *lbl_sol_total_power, *lbl_sol_total_kwh;
static lv_obj_t *lbl_sol_total_week, *lbl_sol_total_month;
static lv_obj_t *lbl_sol_src_power[MAX_SOLAR]    = {};
static lv_obj_t *lbl_sol_src_kwh[MAX_SOLAR]      = {};
static lv_obj_t *lbl_sol_src_week[MAX_SOLAR]     = {};
static lv_obj_t *lbl_sol_src_month[MAX_SOLAR]    = {};
static lv_obj_t *lbl_sol_src_dc[MAX_SOLAR]       = {};

// ─── Widgets écran réseau ─────────────────────────────────────────────────────
static lv_obj_t *lbl_g_power, *lbl_g_volt, *lbl_g_cur;
static lv_obj_t *lbl_g_today, *lbl_g_week, *lbl_g_month;

// ─── Widgets écran batterie ───────────────────────────────────────────────────
static lv_obj_t *lbl_bd_power[MAX_BATTERIES]  = {};
static lv_obj_t *lbl_bd_volt[MAX_BATTERIES]   = {};
static lv_obj_t *lbl_bd_cur[MAX_BATTERIES]    = {};
static lv_obj_t *lbl_bd_soc[MAX_BATTERIES]    = {};
static lv_obj_t *lbl_bd_today[MAX_BATTERIES]  = {};
static lv_obj_t *lbl_bd_week[MAX_BATTERIES]   = {};
static lv_obj_t *lbl_bd_month[MAX_BATTERIES]  = {};

// ─── Widgets écran routeur ────────────────────────────────────────────────────
static lv_obj_t *lbl_rd_power[MAX_ROUTERS]    = {};
static lv_obj_t *lbl_rd_triac[MAX_ROUTERS]    = {};
static lv_obj_t *lbl_rd_today_kwh[MAX_ROUTERS]= {};
static lv_obj_t *lbl_rd_week_kwh[MAX_ROUTERS] = {};
static lv_obj_t *lbl_rd_month_kwh[MAX_ROUTERS]= {};
static lv_obj_t *lbl_rd_dur[MAX_ROUTERS]      = {};
static lv_obj_t *lbl_rd_wdur[MAX_ROUTERS]     = {};
static lv_obj_t *lbl_rd_mdur[MAX_ROUTERS]     = {};

// ─── Helpers ──────────────────────────────────────────────────────────────────
static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t accent) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, C_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, accent, 0);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_set_style_border_opa(c, LV_OPA_40, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_pad_all(c, 12, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static lv_obj_t *make_label(lv_obj_t *p, const char *txt, const lv_font_t *font,
                             lv_color_t col, lv_align_t align, int x, int y) {
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_align(l, align, x, y);
    return l;
}

static const char *ascii(const char *src, char *buf, size_t sz) {
    static const struct { uint8_t b1, b2; char out; } map[] = {
        {0xC3,0xA0,'a'},{0xC3,0xA1,'a'},{0xC3,0xA2,'a'},{0xC3,0xA4,'a'},
        {0xC3,0xA7,'c'},
        {0xC3,0xA8,'e'},{0xC3,0xA9,'e'},{0xC3,0xAA,'e'},{0xC3,0xAB,'e'},
        {0xC3,0xAC,'i'},{0xC3,0xAD,'i'},{0xC3,0xAE,'i'},
        {0xC3,0xB2,'o'},{0xC3,0xB3,'o'},{0xC3,0xB4,'o'},{0xC3,0xB6,'o'},
        {0xC3,0xB9,'u'},{0xC3,0xBA,'u'},{0xC3,0xBB,'u'},{0xC3,0xBC,'u'},
        {0xC3,0x80,'A'},{0xC3,0x81,'A'},{0xC3,0x87,'C'},
        {0xC3,0x88,'E'},{0xC3,0x89,'E'},{0xC3,0x8A,'E'},
        {0xC3,0x9C,'U'},
        {0xC5,0x93,'o'},{0xC5,0x92,'O'},
        {0xC3,0xA6,'a'},{0xC3,0x86,'A'},
        {0xC3,0xB1,'n'},
    };
    const uint8_t *s = (const uint8_t *)src;
    size_t di = 0;
    while (*s && di + 1 < sz) {
        if (*s < 0x80) {
            buf[di++] = (char)*s++;
        } else if (*s >= 0xC0 && s[1]) {
            bool found = false;
            for (auto &e : map) {
                if (s[0] == e.b1 && s[1] == e.b2) {
                    buf[di++] = e.out; s += 2; found = true; break;
                }
            }
            if (!found) { s++; while (*s && (*s & 0xC0) == 0x80) s++; }
        } else { s++; }
    }
    buf[di] = '\0';
    return buf;
}

static void fmt_power(char *buf, size_t sz, float w) {
    if (fabsf(w) >= 1000.0f)
        snprintf(buf, sz, "%.2f kW", w / 1000.0f);
    else
        snprintf(buf, sz, "%.0f W", w);
}

static void fmt_dur(char *buf, size_t sz, float h) {
    if (h <= 0) { snprintf(buf, sz, "--"); return; }
    int hh = (int)h;
    int mm = (int)((h - hh) * 60.0f + 0.5f);
    snprintf(buf, sz, "%02dh%02d", hh, mm);
}

static void scr_setup(lv_obj_t *scr) {
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

// Barre de navigation (boutons Retour + titre) sur les écrans de détail
static void make_nav_bar(lv_obj_t *scr, const char *title) {
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, 800, 40);
    lv_obj_set_style_bg_color(bar, C_CARD, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn = lv_btn_create(bar);
    lv_obj_set_pos(btn, 8, 4);
    lv_obj_set_size(btn, 80, 32);
    lv_obj_set_style_bg_color(btn, C_BORDER, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_add_event_cb(btn, [](lv_event_t *) { ui_navigate(0); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, "< Retour");
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bl, C_TEXT, 0);
    lv_obj_center(bl);

    lv_obj_t *tl = lv_label_create(bar);
    lv_label_set_text(tl, title);
    lv_obj_set_style_text_font(tl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(tl, C_TEXT, 0);
    lv_obj_align(tl, LV_ALIGN_CENTER, 0, 0);
}

// ─── Écran solaire ─────────────────────────────────────────────────────────────
static void create_scr_solar() {
    scr_solar = lv_obj_create(NULL);
    scr_setup(scr_solar);
    make_nav_bar(scr_solar, "SOLAIRE");

    // Carte total
    lv_obj_t *tot = make_card(scr_solar, MARGIN, 48, 800 - 2 * MARGIN, 68, C_SOLAR);
    lbl_sol_total_power = make_label(tot, "--", &lv_font_montserrat_32, C_SOLAR, LV_ALIGN_LEFT_MID, 0, 0);
    lbl_sol_total_kwh   = make_label(tot, "Auj: --", &lv_font_montserrat_16, C_MUTED, LV_ALIGN_LEFT_MID, 180, -10);
    lbl_sol_total_week  = make_label(tot, "Sem: --", &lv_font_montserrat_16, C_MUTED, LV_ALIGN_LEFT_MID, 320, -10);
    lbl_sol_total_month = make_label(tot, "Mois: --", &lv_font_montserrat_16, C_MUTED, LV_ALIGN_LEFT_MID, 470, -10);

    // Cartes sources
    int n_active = 0;
    for (int i = 0; i < MAX_SOLAR; i++)
        if (g_cfg.solars[i].device != SolarDevice::NONE) n_active++;
    if (n_active == 0) n_active = 1;

    int card_w = (800 - 2 * MARGIN - (n_active - 1) * GAP) / n_active;
    int cx = MARGIN;
    int ch = 480 - 48 - 68 - 2 * MARGIN - 2 * GAP;

    for (int i = 0; i < MAX_SOLAR; i++) {
        if (g_cfg.solars[i].device == SolarDevice::NONE) {
            lbl_sol_src_power[i] = lbl_sol_src_kwh[i] = nullptr;
            lbl_sol_src_week[i]  = lbl_sol_src_month[i] = lbl_sol_src_dc[i] = nullptr;
            continue;
        }
        char _nb[40];
        const char *name = ascii(g_cfg.solars[i].name[0] ? g_cfg.solars[i].name : "Source", _nb, sizeof(_nb));
        lv_obj_t *c = make_card(scr_solar, cx, 48 + 68 + GAP, card_w, ch, C_SOLAR);
        make_label(c, name, &lv_font_montserrat_14, C_SOLAR, LV_ALIGN_TOP_LEFT, 0, 0);
        lbl_sol_src_power[i] = make_label(c, "--", &lv_font_montserrat_32, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 22);
        lbl_sol_src_kwh[i]   = make_label(c, "Auj: --", &lv_font_montserrat_14, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 68);
        lbl_sol_src_week[i]  = make_label(c, "Sem: --", &lv_font_montserrat_14, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 90);
        lbl_sol_src_month[i] = make_label(c, "Mois: --", &lv_font_montserrat_14, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 112);
        lbl_sol_src_dc[i]    = make_label(c, "", &lv_font_montserrat_14, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 136);
        cx += card_w + GAP;
    }
}

// ─── Écran réseau ──────────────────────────────────────────────────────────────
static void create_scr_grid() {
    scr_grid = lv_obj_create(NULL);
    scr_setup(scr_grid);
    make_nav_bar(scr_grid, "RESEAU");

    lv_obj_t *c = make_card(scr_grid, MARGIN, 48, 800 - 2 * MARGIN, 480 - 48 - 2 * MARGIN, C_GRID);
    lbl_g_power = make_label(c, "--",            &lv_font_montserrat_48, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 0);
    lbl_g_volt  = make_label(c, "Tension: --",   &lv_font_montserrat_20, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 68);
    lbl_g_cur   = make_label(c, "Intensite: --", &lv_font_montserrat_20, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 96);
    make_label(c, "────────────────────────────────────────",
               &lv_font_montserrat_14, C_BORDER, LV_ALIGN_TOP_LEFT, 0, 130);
    lbl_g_today = make_label(c, "Auj: --",   &lv_font_montserrat_20, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 144);
    lbl_g_week  = make_label(c, "Sem: --",   &lv_font_montserrat_20, C_MUTED, LV_ALIGN_TOP_LEFT, 240, 144);
    lbl_g_month = make_label(c, "Mois: --",  &lv_font_montserrat_20, C_MUTED, LV_ALIGN_TOP_LEFT, 480, 144);
}

// ─── Écran batterie ────────────────────────────────────────────────────────────
static void create_scr_bat() {
    scr_bat = lv_obj_create(NULL);
    scr_setup(scr_bat);
    make_nav_bar(scr_bat, "BATTERIES");

    int n_active = 0;
    for (int i = 0; i < MAX_BATTERIES; i++)
        if (g_cfg.batteries[i].device != BatteryDevice::NONE) n_active++;
    if (n_active == 0) n_active = 1;

    int card_w = (800 - 2 * MARGIN - (n_active - 1) * GAP) / n_active;
    int ch = 480 - 48 - 2 * MARGIN;
    int cx = MARGIN;

    for (int i = 0; i < MAX_BATTERIES; i++) {
        if (g_cfg.batteries[i].device == BatteryDevice::NONE) {
            lbl_bd_power[i] = lbl_bd_volt[i] = lbl_bd_cur[i] = nullptr;
            lbl_bd_soc[i] = lbl_bd_today[i] = lbl_bd_week[i] = lbl_bd_month[i] = nullptr;
            continue;
        }
        char _nb[40];
        const char *name = ascii(g_cfg.batteries[i].name[0] ? g_cfg.batteries[i].name : "Batterie", _nb, sizeof(_nb));
        lv_obj_t *c = make_card(scr_bat, cx, 48, card_w, ch, C_SOLAR);
        make_label(c, name, &lv_font_montserrat_14, C_SOLAR, LV_ALIGN_TOP_LEFT, 0, 0);
        lbl_bd_power[i] = make_label(c, "--",            &lv_font_montserrat_32, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 22);
        lbl_bd_volt[i]  = make_label(c, "V: --",         &lv_font_montserrat_16, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 68);
        lbl_bd_cur[i]   = make_label(c, "A: --",         &lv_font_montserrat_16, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 90);
        lbl_bd_soc[i]   = make_label(c, "SOC: --",       &lv_font_montserrat_16, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 112);
        lbl_bd_today[i] = make_label(c, "Auj: --",       &lv_font_montserrat_14, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 140);
        lbl_bd_week[i]  = make_label(c, "Sem: --",       &lv_font_montserrat_14, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 160);
        lbl_bd_month[i] = make_label(c, "Mois: --",      &lv_font_montserrat_14, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 180);
        cx += card_w + GAP;
    }
}

// ─── Écran routeur ─────────────────────────────────────────────────────────────
static void create_scr_router() {
    scr_router = lv_obj_create(NULL);
    scr_setup(scr_router);
    make_nav_bar(scr_router, "ROUTEURS");

    int n_active = 0;
    for (int i = 0; i < MAX_ROUTERS; i++)
        if (g_cfg.routers[i].device != RouterDevice::NONE) n_active++;
    if (g_cfg.grid_device == GridDevice::F1ATB && n_active == 0) n_active = 1;
    if (n_active == 0) n_active = 1;

    int card_w = (800 - 2 * MARGIN - (n_active - 1) * GAP) / n_active;
    int ch = 480 - 48 - 2 * MARGIN;
    int cx = MARGIN;

    for (int i = 0; i < MAX_ROUTERS; i++) {
        bool show = (g_cfg.routers[i].device != RouterDevice::NONE) ||
                    (i == 0 && g_cfg.grid_device == GridDevice::F1ATB);
        if (!show) {
            lbl_rd_power[i] = lbl_rd_triac[i] = nullptr;
            lbl_rd_today_kwh[i] = lbl_rd_week_kwh[i] = lbl_rd_month_kwh[i] = nullptr;
            lbl_rd_dur[i] = lbl_rd_wdur[i] = lbl_rd_mdur[i] = nullptr;
            continue;
        }
        char _nb[40];
        const char *name = ascii(g_cfg.routers[i].name[0] ? g_cfg.routers[i].name : "Routeur", _nb, sizeof(_nb));
        lv_obj_t *c = make_card(scr_router, cx, 48, card_w, ch, C_GREEN);
        make_label(c, name, &lv_font_montserrat_14, C_GREEN, LV_ALIGN_TOP_LEFT, 0, 0);
        lbl_rd_power[i]     = make_label(c, "--",          &lv_font_montserrat_32, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 22);
        lbl_rd_triac[i]     = make_label(c, "Triac: --%",  &lv_font_montserrat_16, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 68);
        lbl_rd_dur[i]       = make_label(c, "Dur auj: --", &lv_font_montserrat_16, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 94);
        lbl_rd_wdur[i]      = make_label(c, "Dur sem: --", &lv_font_montserrat_14, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 126);
        lbl_rd_mdur[i]      = make_label(c, "Dur mois: --",&lv_font_montserrat_14, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 146);
        lbl_rd_today_kwh[i] = make_label(c, "Auj: --",    &lv_font_montserrat_14, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 174);
        lbl_rd_week_kwh[i]  = make_label(c, "Sem: --",    &lv_font_montserrat_14, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 194);
        lbl_rd_month_kwh[i] = make_label(c, "Mois: --",   &lv_font_montserrat_14, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 214);
        cx += card_w + GAP;
    }
}

// ─── ui_create ────────────────────────────────────────────────────────────────
void ui_create() {
    bool solar_enabled = false;
    for (int i = 0; i < MAX_SOLAR; i++)
        if (g_cfg.solars[i].device != SolarDevice::NONE) { solar_enabled = true; break; }

    bool bat_active[MAX_BATTERIES] = {};
    bool rtr_active[MAX_ROUTERS]   = {};
    int  n_bats = 0, n_rtrs = 0;
    for (int i = 0; i < MAX_BATTERIES; i++)
        if (g_cfg.batteries[i].device != BatteryDevice::NONE) { bat_active[i] = true; n_bats++; }
    for (int i = 0; i < MAX_ROUTERS; i++)
        if (g_cfg.routers[i].device != RouterDevice::NONE) { rtr_active[i] = true; n_rtrs++; }
    if (g_cfg.grid_device == GridDevice::F1ATB && !rtr_active[0]) { rtr_active[0] = true; n_rtrs++; }

    int  n_devs  = n_bats + n_rtrs;
    bool any_dev = (n_devs > 0);
    int  main_h  = any_dev ? MAIN_H : TOTAL_H;
    int  grid_w  = solar_enabled ? CARD_W_2 : (800 - 2 * MARGIN);

    // Calcul du max_w solaire total (somme de toutes les sources)
    int total_solar_max_w = 0;
    for (int i = 0; i < MAX_SOLAR; i++) total_solar_max_w += g_cfg.solars[i].max_w;
    if (total_solar_max_w == 0) total_solar_max_w = 1000;

    // Réinitialise les pointeurs
    memset(lbl_bat_power,    0, sizeof(lbl_bat_power));
    memset(lbl_bat_soc,      0, sizeof(lbl_bat_soc));
    memset(lbl_bat_status,   0, sizeof(lbl_bat_status));
    memset(lbl_rtr_power,    0, sizeof(lbl_rtr_power));
    memset(lbl_rtr_duration, 0, sizeof(lbl_rtr_duration));
    memset(lbl_rtr_kwh,      0, sizeof(lbl_rtr_kwh));
    memset(lbl_rtr_status,   0, sizeof(lbl_rtr_status));
    solar_bar = solar_bar_pct = nullptr;
    lbl_autoconso = nullptr;
    lbl_solar_status = lbl_solar_power = lbl_solar_kwh = nullptr;
    lbl_solar_dc = lbl_solar_limit = nullptr;

    // ── Écran principal ───────────────────────────────────────────────────────
    scr_main = lv_obj_create(NULL);
    scr_setup(scr_main);

    char _nb[40];

    // Carte RESEAU
    {
        lv_obj_t *c = make_card(scr_main, MARGIN, MARGIN, grid_w, main_h, C_GRID);
        lv_obj_add_event_cb(c, [](lv_event_t *) { ui_navigate(2); }, LV_EVENT_CLICKED, nullptr);
        lv_obj_set_style_bg_opa(c, LV_OPA_70, LV_STATE_PRESSED);
        make_label(c, ascii(g_cfg.grid_name[0] ? g_cfg.grid_name : "Reseau", _nb, sizeof(_nb)),
                   &lv_font_montserrat_16, C_GRID, LV_ALIGN_TOP_LEFT, 0, 0);
        lbl_grid_status = make_label(c, "--", &lv_font_montserrat_14, C_MUTED, LV_ALIGN_TOP_RIGHT, 0, 2);
        lbl_grid_power  = make_label(c, "--", &lv_font_montserrat_48, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 28);
        lbl_grid_kwh    = make_label(c, "Auj : -- kWh", &lv_font_montserrat_20, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 96);
        make_label(c, "(+) import  /  (-) export",
                   &lv_font_montserrat_14, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 128);
        lv_obj_t *tag = lv_label_create(c);
        lv_label_set_text(tag, g_cfg.grid_host[0]
            ? grid_device_label(g_cfg.grid_device)
            : "Non configure — voir http://[IP]/");
        lv_obj_set_style_text_font(tag, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(tag, C_MUTED, 0);
        lv_obj_set_width(tag, grid_w - 24);
        lv_label_set_long_mode(tag, LV_LABEL_LONG_WRAP);
        lv_obj_align(tag, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }

    // Carte SOLAIRE (agrégat)
    if (solar_enabled) {
        lv_obj_t *c = make_card(scr_main, MARGIN + CARD_W_2 + GAP, MARGIN, CARD_W_2, main_h, C_SOLAR);
        lv_obj_add_event_cb(c, [](lv_event_t *) { ui_navigate(1); }, LV_EVENT_CLICKED, nullptr);
        lv_obj_set_style_bg_opa(c, LV_OPA_70, LV_STATE_PRESSED);
        make_label(c, ascii(g_cfg.solar_name[0] ? g_cfg.solar_name : "Solaire", _nb, sizeof(_nb)),
                   &lv_font_montserrat_16, C_SOLAR, LV_ALIGN_TOP_LEFT, 0, 0);
        lbl_solar_status = make_label(c, "--", &lv_font_montserrat_14, C_MUTED, LV_ALIGN_TOP_RIGHT, 0, 2);
        lbl_solar_power  = make_label(c, "--", &lv_font_montserrat_48, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 28);
        lbl_solar_kwh    = make_label(c, "Auj : -- kWh", &lv_font_montserrat_20, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 96);

        solar_bar_pct = make_label(c, "0%", &lv_font_montserrat_20, C_MUTED, LV_ALIGN_TOP_RIGHT, 0, 96);
        solar_bar = lv_bar_create(c);
        lv_obj_set_size(solar_bar, CARD_W_2 - 24, 10);
        lv_bar_set_range(solar_bar, 0, total_solar_max_w);
        lv_bar_set_value(solar_bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(solar_bar, C_BORDER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(solar_bar, C_SOLAR,  LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(solar_bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(solar_bar, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(solar_bar, 4, LV_PART_MAIN);
        lv_obj_set_style_radius(solar_bar, 4, LV_PART_INDICATOR);
        lv_obj_set_style_border_width(solar_bar, 0, LV_PART_MAIN);
        lv_obj_align(solar_bar, LV_ALIGN_TOP_LEFT, 0, 130);

        lbl_solar_dc    = make_label(c, "", &lv_font_montserrat_16, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 148);
        lbl_solar_limit = make_label(c, "", &lv_font_montserrat_16, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 172);

        lbl_autoconso = lv_label_create(c);
        lv_label_set_text(lbl_autoconso, "");
        lv_obj_set_style_text_font(lbl_autoconso, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl_autoconso, C_GREEN, 0);
        lv_obj_set_width(lbl_autoconso, lv_pct(100));
        lv_obj_align(lbl_autoconso, LV_ALIGN_TOP_LEFT, 0, 196);

        // Source label (première source active ou "N sources")
        int n_solar = 0;
        for (int i = 0; i < MAX_SOLAR; i++)
            if (g_cfg.solars[i].device != SolarDevice::NONE) n_solar++;
        lv_obj_t *tag = lv_label_create(c);
        char tag_buf[64];
        if (n_solar == 1) {
            for (int i = 0; i < MAX_SOLAR; i++) {
                if (g_cfg.solars[i].device != SolarDevice::NONE) {
                    snprintf(tag_buf, sizeof(tag_buf), "%s", solar_device_label(g_cfg.solars[i].device));
                    break;
                }
            }
        } else {
            snprintf(tag_buf, sizeof(tag_buf), "%d sources configurees", n_solar);
        }
        lv_label_set_text(tag, tag_buf);
        lv_obj_set_style_text_font(tag, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(tag, C_MUTED, 0);
        lv_obj_set_width(tag, CARD_W_2 - 24);
        lv_label_set_long_mode(tag, LV_LABEL_LONG_WRAP);
        lv_obj_align(tag, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }

    // Rangée appareils
    if (any_dev) {
        int dev_w = (800 - 2 * MARGIN - (n_devs - 1) * GAP) / n_devs;
        int dev_y = MARGIN + main_h + GAP;
        int dev_x = MARGIN;

        auto clabel = [](lv_obj_t *p, const char *txt, const lv_font_t *font, lv_color_t col, int y) {
            lv_obj_t *l = lv_label_create(p);
            lv_label_set_text(l, txt);
            lv_obj_set_style_text_font(l, font, 0);
            lv_obj_set_style_text_color(l, col, 0);
            lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_width(l, lv_pct(100));
            lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, y);
            return l;
        };

        const lv_font_t *pwr_font = (n_devs <= 2) ? &lv_font_montserrat_48 : &lv_font_montserrat_32;
        int sec_y   = (n_devs <= 2) ? 88  : 72;
        int third_y = (n_devs <= 2) ? 112 : 96;

        for (int i = 0; i < MAX_BATTERIES; i++) {
            if (!bat_active[i]) continue;
            const char *title = ascii(g_cfg.batteries[i].name[0] ? g_cfg.batteries[i].name : "Batterie", _nb, sizeof(_nb));
            lv_obj_t *c = make_card(scr_main, dev_x, dev_y, dev_w, DEV_H, C_SOLAR);
            lv_obj_add_event_cb(c, [](lv_event_t *) { ui_navigate(3); }, LV_EVENT_CLICKED, nullptr);
            lv_obj_set_style_bg_opa(c, LV_OPA_70, LV_STATE_PRESSED);
            clabel(c, title, &lv_font_montserrat_14, C_SOLAR, 0);
            lbl_bat_status[i] = make_label(c, "--", &lv_font_montserrat_12, C_MUTED, LV_ALIGN_TOP_RIGHT, 0, 2);
            lbl_bat_power[i]  = clabel(c, "--", pwr_font, C_MUTED, 24);
            lbl_bat_soc[i]    = clabel(c, "SOC --", &lv_font_montserrat_16, C_MUTED, sec_y);
            dev_x += dev_w + GAP;
        }
        for (int i = 0; i < MAX_ROUTERS; i++) {
            if (!rtr_active[i]) continue;
            const char *title = ascii(g_cfg.routers[i].name[0] ? g_cfg.routers[i].name : "Routeur", _nb, sizeof(_nb));
            lv_obj_t *c = make_card(scr_main, dev_x, dev_y, dev_w, DEV_H, C_GREEN);
            lv_obj_add_event_cb(c, [](lv_event_t *) { ui_navigate(4); }, LV_EVENT_CLICKED, nullptr);
            lv_obj_set_style_bg_opa(c, LV_OPA_70, LV_STATE_PRESSED);
            clabel(c, title, &lv_font_montserrat_14, C_GREEN, 0);
            lbl_rtr_status[i]   = make_label(c, "--", &lv_font_montserrat_12, C_MUTED, LV_ALIGN_TOP_RIGHT, 0, 2);
            lbl_rtr_power[i]    = clabel(c, "--", pwr_font, C_MUTED, 24);
            lbl_rtr_duration[i] = clabel(c, "", &lv_font_montserrat_14, C_MUTED, sec_y);
            lbl_rtr_kwh[i]      = clabel(c, "", &lv_font_montserrat_14, C_MUTED, third_y);
            dev_x += dev_w + GAP;
        }
    }

    // Liseré indicateur
    ind_strip = lv_obj_create(scr_main);
    lv_obj_set_pos(ind_strip, 0, 0);
    lv_obj_set_size(ind_strip, 800, 12);
    lv_obj_set_style_bg_color(ind_strip, C_MUTED, 0);
    lv_obj_set_style_bg_opa(ind_strip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(ind_strip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(ind_strip, 0, 0);
    lv_obj_set_style_pad_all(ind_strip, 0, 0);
    lv_obj_clear_flag(ind_strip, LV_OBJ_FLAG_SCROLLABLE);

    // Barre IP
    lbl_ip_bar = lv_label_create(scr_main);
    lv_obj_set_style_text_font(lbl_ip_bar, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_ip_bar, C_MUTED, 0);
    lv_label_set_text(lbl_ip_bar, "Config : http://... (connexion WiFi en cours)");
    lv_obj_align(lbl_ip_bar, LV_ALIGN_BOTTOM_MID, 0, -4);

    // ── Écrans de détail ──────────────────────────────────────────────────────
    create_scr_solar();
    create_scr_grid();
    create_scr_bat();
    create_scr_router();

    lv_scr_load(scr_main);
    Serial.printf("[ui] ui_create done — %d bats, %d rtrs, %d solar, main_h=%d\n",
                  n_bats, n_rtrs,
                  []{int n=0; for(int i=0;i<MAX_SOLAR;i++) if(g_cfg.solars[i].device!=SolarDevice::NONE) n++; return n;}(),
                  main_h);
}

// ─── ui_navigate ─────────────────────────────────────────────────────────────
void ui_navigate(int screen_id) {
    nav_last_ms = millis();
    switch (screen_id) {
        case 1: if (scr_solar)  lv_scr_load(scr_solar);  break;
        case 2: if (scr_grid)   lv_scr_load(scr_grid);   break;
        case 3: if (scr_bat)    lv_scr_load(scr_bat);    break;
        case 4: if (scr_router) lv_scr_load(scr_router); break;
        default: if (scr_main)  lv_scr_load(scr_main);   break;
    }
}

void ui_navigate_request(int screen_id) {
    s_nav_request = screen_id;
}

void ui_process_nav_request() {
    int req = s_nav_request;
    if (req < 0) return;
    s_nav_request = -1;
    ui_navigate(req);
}

void ui_auto_return_check() {
    if (!scr_main) return;
    if (lv_scr_act() != scr_main && (millis() - nav_last_ms) > 30000) {
        lv_scr_load(scr_main);
    }
}

// ─── ui_update ────────────────────────────────────────────────────────────────
void ui_update(const AppData &d) {
    char buf[64];

    // ── Réseau (écran principal) ──────────────────────────────────────────────
    if (d.grid.online) {
        lv_label_set_text(lbl_grid_status, LV_SYMBOL_OK "  En ligne");
        lv_obj_set_style_text_color(lbl_grid_status, C_GREEN, 0);
        fmt_power(buf, sizeof(buf), d.grid.power_w);
        lv_label_set_text(lbl_grid_power, buf);
        lv_obj_set_style_text_color(lbl_grid_power, d.grid.power_w > 50 ? C_DANGER : C_GREEN, 0);
        snprintf(buf, sizeof(buf), "Auj : %.2f kWh", d.grid.today_kwh);
        lv_label_set_text(lbl_grid_kwh, buf);
    } else {
        lv_label_set_text(lbl_grid_status, LV_SYMBOL_CLOSE "  Hors ligne");
        lv_obj_set_style_text_color(lbl_grid_status, C_DANGER, 0);
        lv_label_set_text(lbl_grid_power, "--");
        lv_obj_set_style_text_color(lbl_grid_power, C_MUTED, 0);
        lv_label_set_text(lbl_grid_kwh, "Auj : -- kWh");
    }

    // ── Solaire agrégé (écran principal) ─────────────────────────────────────
    if (lbl_solar_status) {
        if (d.solar.online) {
            lv_label_set_text(lbl_solar_status, LV_SYMBOL_OK "  En ligne");
            lv_obj_set_style_text_color(lbl_solar_status, C_GREEN, 0);
            fmt_power(buf, sizeof(buf), d.solar.power_w);
            lv_label_set_text(lbl_solar_power, buf);
            lv_obj_set_style_text_color(lbl_solar_power, C_SOLAR, 0);
            snprintf(buf, sizeof(buf), "Auj : %.2f kWh", d.solar.today_kwh);
            lv_label_set_text(lbl_solar_kwh, buf);

            if (lbl_solar_dc)    lv_label_set_text(lbl_solar_dc, "");
            if (lbl_solar_limit) lv_label_set_text(lbl_solar_limit, "");
        } else {
            lv_label_set_text(lbl_solar_status, LV_SYMBOL_CLOSE "  Hors ligne");
            lv_obj_set_style_text_color(lbl_solar_status, C_DANGER, 0);
            lv_label_set_text(lbl_solar_power, "--");
            lv_obj_set_style_text_color(lbl_solar_power, C_MUTED, 0);
            lv_label_set_text(lbl_solar_kwh, "Auj : -- kWh");
            if (lbl_solar_dc) { lv_label_set_text(lbl_solar_dc, ""); if (lbl_solar_limit) lv_label_set_text(lbl_solar_limit, ""); }
        }
    }

    // Autoconsommation
    if (lbl_autoconso) {
        float solar_prod = fabsf(d.solar.power_w);
        if (d.solar.online && solar_prod > 0) {
            float self_consumed = solar_prod + fminf(0.0f, d.grid.power_w);
            if (self_consumed < 0) self_consumed = 0;
            float total_load = solar_prod + d.grid.power_w;
            float autoconso  = fminf(100.0f, self_consumed / solar_prod * 100.0f);
            float autosuff   = total_load > 0 ? fminf(100.0f, self_consumed / total_load * 100.0f) : 100.0f;
            snprintf(buf, sizeof(buf), "Autoconso %.0f%%   Autonomie %.0f%%", autoconso, autosuff);
            lv_label_set_text(lbl_autoconso, buf);
        } else {
            lv_label_set_text(lbl_autoconso, "");
        }
    }

    // Barre solaire
    if (solar_bar) {
        int total_max_w = 0;
        for (int i = 0; i < MAX_SOLAR; i++) total_max_w += g_cfg.solars[i].max_w;
        if (total_max_w == 0) total_max_w = 1000;
        int val = (int)fabsf(d.solar.power_w);
        if (val > total_max_w) val = total_max_w;
        lv_bar_set_range(solar_bar, 0, total_max_w);
        lv_bar_set_value(solar_bar, val, LV_ANIM_OFF);
        int pct = val * 100 / total_max_w;
        snprintf(buf, sizeof(buf), "%d%%", pct);
        lv_label_set_text(solar_bar_pct, buf);
        lv_obj_set_style_bg_color(solar_bar, (pct >= 80) ? C_GREEN : C_SOLAR, LV_PART_INDICATOR);
    }

    // ── Batteries (écran principal) ───────────────────────────────────────────
    for (int i = 0; i < MAX_BATTERIES; i++) {
        if (!lbl_bat_power[i]) continue;
        const auto &bat = d.batteries[i];
        if (lbl_bat_status[i]) {
            lv_label_set_text(lbl_bat_status[i], bat.online ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
            lv_obj_set_style_text_color(lbl_bat_status[i], bat.online ? C_GREEN : C_DANGER, 0);
        }
        if (bat.online) {
            fmt_power(buf, sizeof(buf), fabsf(bat.power_w));
            char pbuf[32];
            snprintf(pbuf, sizeof(pbuf), "%s%s", bat.power_w >= 0 ? "+" : "-", buf);
            lv_label_set_text(lbl_bat_power[i], pbuf);
            lv_obj_set_style_text_color(lbl_bat_power[i], bat.power_w >= 0 ? C_SOLAR : C_GREEN, 0);
            snprintf(buf, sizeof(buf), "SOC : %.0f%%", bat.soc_pct);
            lv_label_set_text(lbl_bat_soc[i], buf);
            lv_obj_set_style_text_color(lbl_bat_soc[i], bat.soc_pct > 20 ? C_GREEN : C_DANGER, 0);
        } else {
            lv_label_set_text(lbl_bat_power[i], "--");
            lv_obj_set_style_text_color(lbl_bat_power[i], C_MUTED, 0);
            lv_label_set_text(lbl_bat_soc[i], "SOC : --");
            lv_obj_set_style_text_color(lbl_bat_soc[i], C_MUTED, 0);
        }
    }

    // ── Routeurs (écran principal) ────────────────────────────────────────────
    for (int i = 0; i < MAX_ROUTERS; i++) {
        if (!lbl_rtr_power[i]) continue;
        const auto &rtr = d.routers[i];
        if (lbl_rtr_status[i]) {
            lv_label_set_text(lbl_rtr_status[i], rtr.online ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
            lv_obj_set_style_text_color(lbl_rtr_status[i], rtr.online ? C_GREEN : C_DANGER, 0);
        }
        if (rtr.online) {
            bool has_power = g_cfg.routers[i].power_entity[0] != '\0';
            if (has_power && rtr.power_w > 0) fmt_power(buf, sizeof(buf), rtr.power_w);
            else if (rtr.triac_pct > 0)       snprintf(buf, sizeof(buf), "%.0f%%", rtr.triac_pct);
            else                              strcpy(buf, "--");
            lv_obj_set_style_text_color(lbl_rtr_power[i], rtr.active ? C_SOLAR : C_GREEN, 0);
            lv_label_set_text(lbl_rtr_power[i], buf);
            if (lbl_rtr_duration[i]) {
                if (rtr.duration_h > 0) {
                    int hh = (int)rtr.duration_h;
                    int mm = (int)((rtr.duration_h - hh) * 60.0f + 0.5f);
                    if (has_power && rtr.triac_pct > 0)
                        snprintf(buf, sizeof(buf), "%02dh%02d  %.0f%%", hh, mm, rtr.triac_pct);
                    else
                        snprintf(buf, sizeof(buf), "%02dh%02d", hh, mm);
                } else { buf[0] = '\0'; }
                lv_label_set_text(lbl_rtr_duration[i], buf);
            }
            if (lbl_rtr_kwh[i]) {
                if (rtr.today_kwh > 0) snprintf(buf, sizeof(buf), "Auj : %.2f kWh", rtr.today_kwh);
                else buf[0] = '\0';
                lv_label_set_text(lbl_rtr_kwh[i], buf);
            }
        } else {
            lv_label_set_text(lbl_rtr_power[i], "--");
            lv_obj_set_style_text_color(lbl_rtr_power[i], C_MUTED, 0);
            if (lbl_rtr_duration[i]) lv_label_set_text(lbl_rtr_duration[i], "");
            if (lbl_rtr_kwh[i])      lv_label_set_text(lbl_rtr_kwh[i], "");
        }
    }

    // ── Liseré ────────────────────────────────────────────────────────────────
    if (ind_strip) {
        lv_color_t col = C_MUTED;
        if      (d.grid.online && d.grid.power_w < -10) col = C_GREEN;
        else if (d.grid.online && d.grid.power_w >  10) col = C_DANGER;
        lv_obj_set_style_bg_color(ind_strip, col, 0);
    }

    // ── Barre IP ──────────────────────────────────────────────────────────────
    if (WiFi.status() == WL_CONNECTED) {
        snprintf(buf, sizeof(buf), "Config : http://%s/", WiFi.localIP().toString().c_str());
        lv_label_set_text(lbl_ip_bar, buf);
    } else if (ap_active) {
        lv_label_set_text(lbl_ip_bar, "AP: DashEnergy-Config — http://192.168.4.1/");
    }

    // ── Écran SOLAIRE ─────────────────────────────────────────────────────────
    if (lbl_sol_total_power) {
        fmt_power(buf, sizeof(buf), d.solar.power_w);
        lv_label_set_text(lbl_sol_total_power, buf);
        lv_obj_set_style_text_color(lbl_sol_total_power, d.solar.online ? C_SOLAR : C_MUTED, 0);
        snprintf(buf, sizeof(buf), "Auj: %.2f kWh", d.solar.today_kwh);
        lv_label_set_text(lbl_sol_total_kwh, buf);
        snprintf(buf, sizeof(buf), "Sem: %.2f kWh", d.solar.week_kwh);
        lv_label_set_text(lbl_sol_total_week, buf);
        snprintf(buf, sizeof(buf), "Mois: %.2f kWh", d.solar.month_kwh);
        lv_label_set_text(lbl_sol_total_month, buf);
    }
    for (int i = 0; i < MAX_SOLAR; i++) {
        if (!lbl_sol_src_power[i]) continue;
        const auto &s = d.solar_src[i];
        fmt_power(buf, sizeof(buf), s.power_w);
        lv_label_set_text(lbl_sol_src_power[i], buf);
        lv_obj_set_style_text_color(lbl_sol_src_power[i], s.online ? C_SOLAR : C_MUTED, 0);
        snprintf(buf, sizeof(buf), "Auj: %.2f kWh", s.today_kwh);
        lv_label_set_text(lbl_sol_src_kwh[i], buf);
        snprintf(buf, sizeof(buf), "Sem: %.2f kWh", s.week_kwh);
        lv_label_set_text(lbl_sol_src_week[i], buf);
        snprintf(buf, sizeof(buf), "Mois: %.2f kWh", s.month_kwh);
        lv_label_set_text(lbl_sol_src_month[i], buf);
        if (lbl_sol_src_dc[i]) {
            if (s.limit_str[0])         { snprintf(buf, sizeof(buf), "Lim: %s", s.limit_str); }
            else if (s.voltage_v > 0)   { snprintf(buf, sizeof(buf), "%.1f V  %.2f A", s.voltage_v, s.current_a); }
            else                        { buf[0] = '\0'; }
            lv_label_set_text(lbl_sol_src_dc[i], buf);
        }
    }

    // ── Écran RÉSEAU ──────────────────────────────────────────────────────────
    if (lbl_g_power) {
        fmt_power(buf, sizeof(buf), d.grid.power_w);
        lv_label_set_text(lbl_g_power, buf);
        lv_obj_set_style_text_color(lbl_g_power, d.grid.power_w > 50 ? C_DANGER : C_GREEN, 0);
        if (d.grid.voltage_v > 0)
            snprintf(buf, sizeof(buf), "Tension: %.1f V", d.grid.voltage_v);
        else
            snprintf(buf, sizeof(buf), "Tension: --");
        lv_label_set_text(lbl_g_volt, buf);
        if (d.grid.current_a > 0)
            snprintf(buf, sizeof(buf), "Intensite: %.2f A", d.grid.current_a);
        else
            snprintf(buf, sizeof(buf), "Intensite: --");
        lv_label_set_text(lbl_g_cur, buf);
        snprintf(buf, sizeof(buf), "Auj: %.3f kWh", d.grid.today_kwh);
        lv_label_set_text(lbl_g_today, buf);
        snprintf(buf, sizeof(buf), "Sem: %.2f kWh", d.grid.week_kwh);
        lv_label_set_text(lbl_g_week, buf);
        snprintf(buf, sizeof(buf), "Mois: %.2f kWh", d.grid.month_kwh);
        lv_label_set_text(lbl_g_month, buf);
    }

    // ── Écran BATTERIE ────────────────────────────────────────────────────────
    for (int i = 0; i < MAX_BATTERIES; i++) {
        if (!lbl_bd_power[i]) continue;
        const auto &bat = d.batteries[i];
        if (bat.online) {
            fmt_power(buf, sizeof(buf), fabsf(bat.power_w));
            char pbuf[32];
            snprintf(pbuf, sizeof(pbuf), "%s%s", bat.power_w >= 0 ? "+" : "-", buf);
            lv_label_set_text(lbl_bd_power[i], pbuf);
            lv_obj_set_style_text_color(lbl_bd_power[i], bat.power_w >= 0 ? C_SOLAR : C_GREEN, 0);
            snprintf(buf, sizeof(buf), "V: %.1f", bat.voltage_v > 0 ? bat.voltage_v : 0.0f);
            lv_label_set_text(lbl_bd_volt[i], buf);
            snprintf(buf, sizeof(buf), "A: %.2f", bat.current_a);
            lv_label_set_text(lbl_bd_cur[i], buf);
            snprintf(buf, sizeof(buf), "SOC: %.0f%%", bat.soc_pct);
            lv_label_set_text(lbl_bd_soc[i], buf);
            lv_obj_set_style_text_color(lbl_bd_soc[i], bat.soc_pct > 20 ? C_GREEN : C_DANGER, 0);
            snprintf(buf, sizeof(buf), "Auj: %.3f kWh", bat.today_kwh);
            lv_label_set_text(lbl_bd_today[i], buf);
            snprintf(buf, sizeof(buf), "Sem: %.2f kWh", bat.week_kwh);
            lv_label_set_text(lbl_bd_week[i], buf);
            snprintf(buf, sizeof(buf), "Mois: %.2f kWh", bat.month_kwh);
            lv_label_set_text(lbl_bd_month[i], buf);
        } else {
            lv_label_set_text(lbl_bd_power[i], "--");
            lv_label_set_text(lbl_bd_volt[i], "V: --");
            lv_label_set_text(lbl_bd_cur[i], "A: --");
            lv_label_set_text(lbl_bd_soc[i], "SOC: --");
            lv_label_set_text(lbl_bd_today[i], "Auj: --");
            lv_label_set_text(lbl_bd_week[i], "Sem: --");
            lv_label_set_text(lbl_bd_month[i], "Mois: --");
        }
    }

    // ── Écran ROUTEUR ─────────────────────────────────────────────────────────
    for (int i = 0; i < MAX_ROUTERS; i++) {
        if (!lbl_rd_power[i]) continue;
        const auto &rtr = d.routers[i];
        if (rtr.online) {
            bool has_power = g_cfg.routers[i].power_entity[0] != '\0';
            if (has_power) {
                fmt_power(buf, sizeof(buf), rtr.power_w);
            } else {
                // Pas d'entité puissance configurée → triac en valeur principale
                if (rtr.triac_pct > 0) snprintf(buf, sizeof(buf), "%.0f%%", rtr.triac_pct);
                else snprintf(buf, sizeof(buf), "--%");
            }
            lv_label_set_text(lbl_rd_power[i], buf);
            lv_obj_set_style_text_color(lbl_rd_power[i], rtr.active ? C_SOLAR : C_GREEN, 0);
            if (lbl_rd_triac[i]) {
                if (has_power && rtr.triac_pct > 0) snprintf(buf, sizeof(buf), "Triac: %.0f%%", rtr.triac_pct);
                else buf[0] = '\0';
                lv_label_set_text(lbl_rd_triac[i], buf);
            }
            char tmp[64];
            fmt_dur(buf, sizeof(buf), rtr.duration_h);
            snprintf(tmp, sizeof(tmp), "Dur auj: %s", buf);
            lv_label_set_text(lbl_rd_dur[i], tmp);
            fmt_dur(buf, sizeof(buf), rtr.week_duration_h);
            snprintf(tmp, sizeof(tmp), "Dur sem: %s", buf);
            lv_label_set_text(lbl_rd_wdur[i], tmp);
            fmt_dur(buf, sizeof(buf), rtr.month_duration_h);
            snprintf(tmp, sizeof(tmp), "Dur mois: %s", buf);
            lv_label_set_text(lbl_rd_mdur[i], tmp);
            snprintf(buf, sizeof(buf), "Auj: %.3f kWh", rtr.today_kwh);
            lv_label_set_text(lbl_rd_today_kwh[i], buf);
            snprintf(buf, sizeof(buf), "Sem: %.2f kWh", rtr.week_kwh);
            lv_label_set_text(lbl_rd_week_kwh[i], buf);
            snprintf(buf, sizeof(buf), "Mois: %.2f kWh", rtr.month_kwh);
            lv_label_set_text(lbl_rd_month_kwh[i], buf);
        } else {
            lv_label_set_text(lbl_rd_power[i], "--");
            lv_obj_set_style_text_color(lbl_rd_power[i], C_MUTED, 0);
            if (lbl_rd_triac[i]) lv_label_set_text(lbl_rd_triac[i], "Triac: --%");
            lv_label_set_text(lbl_rd_dur[i], "Dur auj: --");
            lv_label_set_text(lbl_rd_wdur[i], "Dur sem: --");
            lv_label_set_text(lbl_rd_mdur[i], "Dur mois: --");
            lv_label_set_text(lbl_rd_today_kwh[i], "Auj: --");
            lv_label_set_text(lbl_rd_week_kwh[i], "Sem: --");
            lv_label_set_text(lbl_rd_month_kwh[i], "Mois: --");
        }
    }
}
