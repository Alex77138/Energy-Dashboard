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
static constexpr int MARGIN     = 8;
static constexpr int GAP        = 8;
static constexpr int BOT_H      = 22;
static constexpr int CARD_H     = 480 - MARGIN - BOT_H;              // 450
static constexpr int CARD_W_2   = (800 - 2 * MARGIN - GAP) / 2;     // 388 (mode 2 cartes)
static constexpr int CARD_W_1   = 800 - 2 * MARGIN;                  // 784 (mode grille seule)

// ─── Widgets ──────────────────────────────────────────────────────────────────
static lv_obj_t *lbl_grid_status, *lbl_grid_power, *lbl_grid_kwh;
static lv_obj_t *lbl_solar_status, *lbl_solar_power, *lbl_solar_kwh;
static lv_obj_t *lbl_solar_dc, *lbl_solar_limit;
static lv_obj_t *lbl_autoconso  = nullptr;
static lv_obj_t *lbl_autosuff   = nullptr;
static lv_obj_t *lbl_ip_bar;
static lv_obj_t *solar_arc     = nullptr;
static lv_obj_t *solar_arc_pct = nullptr;

// ─── Helpers ──────────────────────────────────────────────────────────────────
static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, lv_color_t accent) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, CARD_H);
    lv_obj_set_style_bg_color(c, C_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, accent, 0);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_set_style_border_opa(c, LV_OPA_40, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_pad_all(c, 14, 0);
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

static void fmt_power(char *buf, size_t sz, float w) {
    if (fabsf(w) >= 1000.0f)
        snprintf(buf, sz, "%.2f kW", w / 1000.0f);
    else
        snprintf(buf, sz, "%.0f W", w);
}

// ─── ui_navigate (gardé pour compatibilité, ne fait rien) ─────────────────────
void ui_navigate(bool) {}

// ─── ui_create ────────────────────────────────────────────────────────────────
void ui_create() {
    bool solar_enabled = (g_cfg.solar_device != SolarDevice::NONE);
    int  grid_w  = solar_enabled ? CARD_W_2 : CARD_W_1;

    // Utilise l'écran actif par défaut (évite lv_scr_load qui peut échouer silencieusement)
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clean(scr);  // supprime les enfants existants si besoin
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ── Carte RESEAU ──────────────────────────────────────────────────────────
    {
        lv_obj_t *c = make_card(scr, MARGIN, MARGIN, grid_w, C_GRID);
        make_label(c, "RESEAU", &lv_font_montserrat_14, C_GRID, LV_ALIGN_TOP_LEFT, 0, 0);
        lbl_grid_status = make_label(c, "--", &lv_font_montserrat_12, C_MUTED,
                                      LV_ALIGN_TOP_RIGHT, 0, 2);
        lbl_grid_power  = make_label(c, "--", &lv_font_montserrat_48, C_MUTED,
                                      LV_ALIGN_TOP_LEFT, 0, 44);
        lbl_grid_kwh    = make_label(c, "Auj : -- kWh", &lv_font_montserrat_16, C_MUTED,
                                      LV_ALIGN_TOP_LEFT, 0, 114);
        make_label(c, "(+) import  /  (-) export",
                   &lv_font_montserrat_12, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 148);

        lv_obj_t *tag = lv_label_create(c);
        lv_label_set_text(tag, g_cfg.grid_host[0]
            ? grid_device_label(g_cfg.grid_device)
            : "Non configure — voir http://[IP]/");
        lv_obj_set_style_text_font(tag, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(tag, C_MUTED, 0);
        lv_obj_set_width(tag, grid_w - 28);
        lv_label_set_long_mode(tag, LV_LABEL_LONG_WRAP);
        lv_obj_align(tag, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }

    // ── Carte SOLAIRE (masquée si source non configurée) ──────────────────────
    if (!solar_enabled) {
        // Mode grille seule : pas de carte solaire
        lbl_solar_status = lbl_solar_power = lbl_solar_kwh = nullptr;
        lbl_solar_dc = lbl_solar_limit = nullptr;
        solar_arc = solar_arc_pct = nullptr;
    } else
    {
        lv_obj_t *c = make_card(scr, MARGIN + CARD_W_2 + GAP, MARGIN, CARD_W_2, C_SOLAR);
        make_label(c, "SOLAIRE", &lv_font_montserrat_14, C_SOLAR, LV_ALIGN_TOP_LEFT, 0, 0);
        lbl_solar_status = make_label(c, "--", &lv_font_montserrat_12, C_MUTED,
                                       LV_ALIGN_TOP_RIGHT, 0, 2);

        lbl_solar_power  = make_label(c, "--", &lv_font_montserrat_48, C_MUTED,
                                       LV_ALIGN_TOP_LEFT, 0, 44);
        // Contrainte largeur si la jauge est présente
        if (g_cfg.solar_max_w > 0)
            lv_obj_set_width(lbl_solar_power, 200);

        lbl_solar_kwh    = make_label(c, "Auj : -- kWh", &lv_font_montserrat_16, C_MUTED,
                                       LV_ALIGN_TOP_LEFT, 0, 114);
        lbl_solar_dc     = make_label(c, "", &lv_font_montserrat_14, C_MUTED,
                                       LV_ALIGN_TOP_LEFT, 0, 148);
        lbl_solar_limit  = make_label(c, "", &lv_font_montserrat_14, C_MUTED,
                                       LV_ALIGN_TOP_LEFT, 0, 168);
        lbl_autoconso    = make_label(c, "", &lv_font_montserrat_14, C_GREEN,
                                       LV_ALIGN_TOP_LEFT, 0, 200);
        lbl_autosuff     = make_label(c, "", &lv_font_montserrat_14, C_GREEN,
                                       LV_ALIGN_TOP_LEFT, 0, 222);

        // ── Jauge arc (si puissance max configurée) ──────────────────────────
        if (g_cfg.solar_max_w > 0) {
            solar_arc = lv_arc_create(c);
            lv_obj_set_size(solar_arc, 130, 130);
            lv_arc_set_rotation(solar_arc, 135);
            lv_arc_set_bg_angles(solar_arc, 0, 270);
            lv_arc_set_range(solar_arc, 0, g_cfg.solar_max_w);
            lv_arc_set_value(solar_arc, 0);
            lv_arc_set_mode(solar_arc, LV_ARC_MODE_NORMAL);
            lv_obj_remove_style(solar_arc, nullptr, LV_PART_KNOB);
            lv_obj_clear_flag(solar_arc, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_arc_width(solar_arc, 10, LV_PART_MAIN);
            lv_obj_set_style_arc_width(solar_arc, 10, LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(solar_arc, C_BORDER, LV_PART_MAIN);
            lv_obj_set_style_arc_color(solar_arc, C_SOLAR, LV_PART_INDICATOR);
            lv_obj_set_style_bg_opa(solar_arc, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_align(solar_arc, LV_ALIGN_TOP_RIGHT, 0, 25);

            // % au centre de l'arc
            solar_arc_pct = lv_label_create(solar_arc);
            lv_label_set_text(solar_arc_pct, "0%");
            lv_obj_set_style_text_font(solar_arc_pct, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(solar_arc_pct, C_TEXT, 0);
            lv_obj_align(solar_arc_pct, LV_ALIGN_CENTER, 0, 0);

            // Max Wc en dessous de l'arc
            char maxbuf[16];
            snprintf(maxbuf, sizeof(maxbuf), "%d Wc", g_cfg.solar_max_w);
            lv_obj_t *max_lbl = lv_label_create(c);
            lv_label_set_text(max_lbl, maxbuf);
            lv_obj_set_style_text_font(max_lbl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(max_lbl, C_MUTED, 0);
            lv_obj_align(max_lbl, LV_ALIGN_TOP_RIGHT, 0, 162);
        }

        lv_obj_t *tag = lv_label_create(c);
        lv_label_set_text(tag, g_cfg.solar_host[0]
            ? solar_device_label(g_cfg.solar_device)
            : "Non configure — voir http://[IP]/");
        lv_obj_set_style_text_font(tag, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(tag, C_MUTED, 0);
        lv_obj_set_width(tag, CARD_W_2 - 28);
        lv_label_set_long_mode(tag, LV_LABEL_LONG_WRAP);
        lv_obj_align(tag, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }  // end solar card (else block)

    // ── Barre IP en bas ────────────────────────────────────────────────────────
    lbl_ip_bar = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_ip_bar, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_ip_bar, C_MUTED, 0);
    lv_label_set_text(lbl_ip_bar, "Config : http://... (connexion WiFi en cours)");
    lv_obj_align(lbl_ip_bar, LV_ALIGN_BOTTOM_MID, 0, -4);

    Serial.printf("[ui] ui_create done — scr=%p lv_scr_act=%p\n", scr, lv_scr_act());
}

// ─── ui_update ────────────────────────────────────────────────────────────────
void ui_update(const AppData &d) {
    char buf[48];

    // ── Réseau ────────────────────────────────────────────────────────────────
    if (d.grid.online) {
        lv_label_set_text(lbl_grid_status, LV_SYMBOL_OK "  En ligne");
        lv_obj_set_style_text_color(lbl_grid_status, C_GREEN, 0);
        fmt_power(buf, sizeof(buf), d.grid.power_w);
        lv_label_set_text(lbl_grid_power, buf);
        lv_obj_set_style_text_color(lbl_grid_power,
            d.grid.power_w > 50 ? C_DANGER : C_GREEN, 0);
        snprintf(buf, sizeof(buf), "Auj : %.2f kWh", d.grid.today_kwh);
        lv_label_set_text(lbl_grid_kwh, buf);
    } else {
        lv_label_set_text(lbl_grid_status, LV_SYMBOL_CLOSE "  Hors ligne");
        lv_obj_set_style_text_color(lbl_grid_status, C_DANGER, 0);
        lv_label_set_text(lbl_grid_power, "--");
        lv_obj_set_style_text_color(lbl_grid_power, C_MUTED, 0);
        lv_label_set_text(lbl_grid_kwh, "Auj : -- kWh");
    }

    // ── Solaire ───────────────────────────────────────────────────────────────
    if (lbl_solar_status) {
        if (d.solar.online) {
            lv_label_set_text(lbl_solar_status, LV_SYMBOL_OK "  En ligne");
            lv_obj_set_style_text_color(lbl_solar_status, C_GREEN, 0);
            fmt_power(buf, sizeof(buf), d.solar.power_w);
            lv_label_set_text(lbl_solar_power, buf);
            lv_obj_set_style_text_color(lbl_solar_power, C_SOLAR, 0);
            snprintf(buf, sizeof(buf), "Auj : %.2f kWh", d.solar.today_kwh);
            lv_label_set_text(lbl_solar_kwh, buf);

            bool is_dtu = (g_cfg.solar_device == SolarDevice::OPENDTU ||
                           g_cfg.solar_device == SolarDevice::AHOYDTU);
            if (is_dtu && lbl_solar_dc) {
                snprintf(buf, sizeof(buf), "DC : %.1f V", d.solar.dc_voltage);
                lv_label_set_text(lbl_solar_dc, buf);
                snprintf(buf, sizeof(buf), "Limite : %d %%", d.solar.limit_pct);
                lv_label_set_text(lbl_solar_limit, buf);
            } else if (lbl_solar_dc) {
                lv_label_set_text(lbl_solar_dc, "");
                lv_label_set_text(lbl_solar_limit, "");
            }
        } else {
            lv_label_set_text(lbl_solar_status, LV_SYMBOL_CLOSE "  Hors ligne");
            lv_obj_set_style_text_color(lbl_solar_status, C_DANGER, 0);
            lv_label_set_text(lbl_solar_power, "--");
            lv_obj_set_style_text_color(lbl_solar_power, C_MUTED, 0);
            lv_label_set_text(lbl_solar_kwh, "Auj : -- kWh");
            if (lbl_solar_dc) { lv_label_set_text(lbl_solar_dc, ""); lv_label_set_text(lbl_solar_limit, ""); }
        }
    }

    // ── Autoconsommation / Autosuffisance ─────────────────────────────────────
    if (lbl_autoconso) {
        if (d.solar.online && d.solar.power_w > 0) {
            float self_consumed = d.solar.power_w + fminf(0.0f, d.grid.power_w);
            if (self_consumed < 0) self_consumed = 0;
            float total_load = d.solar.power_w + d.grid.power_w;
            float autoconso  = fminf(100.0f, self_consumed / d.solar.power_w * 100.0f);
            float autosuff   = total_load > 0 ? fminf(100.0f, self_consumed / total_load * 100.0f) : 100.0f;
            snprintf(buf, sizeof(buf), "Autoconso  %.0f%%", autoconso);
            lv_label_set_text(lbl_autoconso, buf);
            snprintf(buf, sizeof(buf), "Autosuff   %.0f%%", autosuff);
            lv_label_set_text(lbl_autosuff, buf);
        } else {
            lv_label_set_text(lbl_autoconso, "");
            lv_label_set_text(lbl_autosuff, "");
        }
    }

    // ── Jauge arc ─────────────────────────────────────────────────────────────
    if (solar_arc && g_cfg.solar_max_w > 0) {
        int val = (int)d.solar.power_w;
        if (val < 0) val = 0;
        if (val > g_cfg.solar_max_w) val = g_cfg.solar_max_w;
        lv_arc_set_value(solar_arc, val);
        int pct = val * 100 / g_cfg.solar_max_w;
        snprintf(buf, sizeof(buf), "%d%%", pct);
        lv_label_set_text(solar_arc_pct, buf);
        // Couleur selon le rendement
        lv_color_t col = pct >= 80 ? C_GREEN : (pct >= 40 ? C_SOLAR : C_MUTED);
        lv_obj_set_style_arc_color(solar_arc, col, LV_PART_INDICATOR);
    }

    // ── Barre IP ──────────────────────────────────────────────────────────────
    if (WiFi.status() == WL_CONNECTED) {
        snprintf(buf, sizeof(buf), "Config : http://%s/",
                 WiFi.localIP().toString().c_str());
        lv_label_set_text(lbl_ip_bar, buf);
    } else if (ap_active) {
        lv_label_set_text(lbl_ip_bar, "AP: DashEnergy-Config — http://192.168.4.1/");
    }
}
