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
static constexpr int TOTAL_H  = 480 - MARGIN - BOT_H;          // 450
static constexpr int DEV_H    = 128;                            // hauteur rangée appareils
static constexpr int MAIN_H   = TOTAL_H - GAP - DEV_H;         // 314 quand appareils présents
static constexpr int CARD_W_2 = (800 - 2 * MARGIN - GAP) / 2; // 388

// ─── Widgets ──────────────────────────────────────────────────────────────────
static lv_obj_t *lbl_grid_status, *lbl_grid_power, *lbl_grid_kwh;
static lv_obj_t *lbl_solar_status, *lbl_solar_power, *lbl_solar_kwh;
static lv_obj_t *lbl_solar_dc, *lbl_solar_limit;
static lv_obj_t *lbl_autoconso    = nullptr;
static lv_obj_t *lbl_autosuff     = nullptr;
static lv_obj_t *solar_bar        = nullptr;
static lv_obj_t *solar_bar_pct    = nullptr;
static lv_obj_t *ind_strip        = nullptr;
static lv_obj_t *lbl_ip_bar;

// Tableaux pour batteries/routeurs (indices alignés sur g_cfg)
static lv_obj_t *lbl_bat_power[MAX_BATTERIES]    = {};
static lv_obj_t *lbl_bat_soc[MAX_BATTERIES]      = {};
static lv_obj_t *lbl_rtr_power[MAX_ROUTERS]      = {};
static lv_obj_t *lbl_rtr_duration[MAX_ROUTERS]   = {};
static lv_obj_t *lbl_rtr_kwh[MAX_ROUTERS]        = {};

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

    // Recense les appareils configurés
    bool bat_active[MAX_BATTERIES] = {};
    bool rtr_active[MAX_ROUTERS]   = {};
    int  n_bats = 0, n_rtrs = 0;
    for (int i = 0; i < MAX_BATTERIES; i++)
        if (g_cfg.batteries[i].device != BatteryDevice::NONE) { bat_active[i] = true; n_bats++; }
    for (int i = 0; i < MAX_ROUTERS; i++)
        if (g_cfg.routers[i].device != RouterDevice::NONE) { rtr_active[i] = true; n_rtrs++; }
    // F1ATB peuple routers[0] si aucun routeur dédié sur slot 0
    if (g_cfg.grid_device == GridDevice::F1ATB && !rtr_active[0]) { rtr_active[0] = true; n_rtrs++; }

    int  n_devs   = n_bats + n_rtrs;
    bool any_dev  = (n_devs > 0);
    int  main_h   = any_dev ? MAIN_H : TOTAL_H;
    int  grid_w   = solar_enabled ? CARD_W_2 : (800 - 2 * MARGIN);

    // Réinitialise les pointeurs
    memset(lbl_bat_power,   0, sizeof(lbl_bat_power));
    memset(lbl_bat_soc,     0, sizeof(lbl_bat_soc));
    memset(lbl_rtr_power,   0, sizeof(lbl_rtr_power));
    memset(lbl_rtr_duration,0, sizeof(lbl_rtr_duration));
    memset(lbl_rtr_kwh,     0, sizeof(lbl_rtr_kwh));
    solar_bar = solar_bar_pct = nullptr;
    lbl_autoconso = lbl_autosuff = nullptr;
    lbl_solar_status = lbl_solar_power = lbl_solar_kwh = nullptr;
    lbl_solar_dc = lbl_solar_limit = nullptr;

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clean(scr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ── Carte RESEAU ──────────────────────────────────────────────────────────
    {
        lv_obj_t *c = make_card(scr, MARGIN, MARGIN, grid_w, main_h, C_GRID);
        make_label(c, g_cfg.grid_name[0] ? g_cfg.grid_name : "R\xc3\xa9seau",
                   &lv_font_montserrat_14, C_GRID, LV_ALIGN_TOP_LEFT, 0, 0);
        lbl_grid_status = make_label(c, "--", &lv_font_montserrat_12, C_MUTED,
                                      LV_ALIGN_TOP_RIGHT, 0, 2);
        lbl_grid_power  = make_label(c, "--", &lv_font_montserrat_48, C_MUTED,
                                      LV_ALIGN_TOP_LEFT, 0, 40);
        lbl_grid_kwh    = make_label(c, "Auj : -- kWh", &lv_font_montserrat_16, C_MUTED,
                                      LV_ALIGN_TOP_LEFT, 0, 110);
        make_label(c, "(+) import  /  (-) export",
                   &lv_font_montserrat_12, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 144);

        lv_obj_t *tag = lv_label_create(c);
        lv_label_set_text(tag, g_cfg.grid_host[0]
            ? grid_device_label(g_cfg.grid_device)
            : "Non configure — voir http://[IP]/");
        lv_obj_set_style_text_font(tag, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(tag, C_MUTED, 0);
        lv_obj_set_width(tag, grid_w - 24);
        lv_label_set_long_mode(tag, LV_LABEL_LONG_WRAP);
        lv_obj_align(tag, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }

    // ── Carte SOLAIRE ─────────────────────────────────────────────────────────
    if (!solar_enabled) {
        // widgets solaires non créés
    } else {
        lv_obj_t *c = make_card(scr, MARGIN + CARD_W_2 + GAP, MARGIN, CARD_W_2, main_h, C_SOLAR);
        make_label(c, g_cfg.solar_name[0] ? g_cfg.solar_name : "Solaire",
                   &lv_font_montserrat_14, C_SOLAR, LV_ALIGN_TOP_LEFT, 0, 0);
        lbl_solar_status = make_label(c, "--", &lv_font_montserrat_12, C_MUTED,
                                       LV_ALIGN_TOP_RIGHT, 0, 2);
        lbl_solar_power  = make_label(c, "--", &lv_font_montserrat_48, C_MUTED,
                                       LV_ALIGN_TOP_LEFT, 0, 40);
        lbl_solar_kwh    = make_label(c, "Auj : -- kWh", &lv_font_montserrat_16, C_MUTED,
                                       LV_ALIGN_TOP_LEFT, 0, 110);

        // Barre de production — visible dès que solaire configuré
        {
            int max_w = (g_cfg.solar_max_w > 0) ? g_cfg.solar_max_w : 1000;
            // % à droite de la ligne kWh
            solar_bar_pct = make_label(c, "0%", &lv_font_montserrat_16, C_MUTED,
                                        LV_ALIGN_TOP_RIGHT, 0, 110);
            // Barre horizontale
            solar_bar = lv_bar_create(c);
            lv_obj_set_size(solar_bar, CARD_W_2 - 24, 10);
            lv_bar_set_range(solar_bar, 0, max_w);
            lv_bar_set_value(solar_bar, 0, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(solar_bar, C_BORDER, LV_PART_MAIN);
            lv_obj_set_style_bg_color(solar_bar, C_SOLAR,  LV_PART_INDICATOR);
            lv_obj_set_style_bg_opa(solar_bar, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(solar_bar, LV_OPA_COVER, LV_PART_INDICATOR);
            lv_obj_set_style_radius(solar_bar, 4, LV_PART_MAIN);
            lv_obj_set_style_radius(solar_bar, 4, LV_PART_INDICATOR);
            lv_obj_set_style_border_width(solar_bar, 0, LV_PART_MAIN);
            lv_obj_align(solar_bar, LV_ALIGN_TOP_LEFT, 0, 134);
        }

        lbl_solar_dc     = make_label(c, "", &lv_font_montserrat_14, C_MUTED,
                                       LV_ALIGN_TOP_LEFT, 0, 152);
        lbl_solar_limit  = make_label(c, "", &lv_font_montserrat_14, C_MUTED,
                                       LV_ALIGN_TOP_LEFT, 0, 172);
        lbl_autoconso    = make_label(c, "", &lv_font_montserrat_14, C_GREEN,
                                       LV_ALIGN_TOP_LEFT, 0, 204);
        lbl_autosuff     = make_label(c, "", &lv_font_montserrat_14, C_GREEN,
                                       LV_ALIGN_TOP_LEFT, 0, 226);

        lv_obj_t *tag = lv_label_create(c);
        lv_label_set_text(tag, g_cfg.solar_host[0]
            ? solar_device_label(g_cfg.solar_device)
            : "Non configure — voir http://[IP]/");
        lv_obj_set_style_text_font(tag, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(tag, C_MUTED, 0);
        lv_obj_set_width(tag, CARD_W_2 - 24);
        lv_label_set_long_mode(tag, LV_LABEL_LONG_WRAP);
        lv_obj_align(tag, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }

    // ── Rangée appareils (batteries + routeurs) ───────────────────────────────
    if (any_dev) {
        int dev_w = (800 - 2 * MARGIN - (n_devs - 1) * GAP) / n_devs;
        int dev_y = MARGIN + main_h + GAP;
        int dev_x = MARGIN;

        // Cartes batteries
        for (int i = 0; i < MAX_BATTERIES; i++) {
            if (!bat_active[i]) continue;
            const char *title = g_cfg.batteries[i].name[0] ? g_cfg.batteries[i].name : "Batterie";
            lv_obj_t *c = make_card(scr, dev_x, dev_y, dev_w, DEV_H, C_SOLAR);
            make_label(c, title, &lv_font_montserrat_12, C_SOLAR, LV_ALIGN_TOP_LEFT, 0, 0);
            lbl_bat_power[i] = make_label(c, "--",    &lv_font_montserrat_14, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 20);
            lbl_bat_soc[i]   = make_label(c, "SOC --",&lv_font_montserrat_14, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 42);
            dev_x += dev_w + GAP;
        }

        // Cartes routeurs
        for (int i = 0; i < MAX_ROUTERS; i++) {
            if (!rtr_active[i]) continue;
            const char *title = g_cfg.routers[i].name[0] ? g_cfg.routers[i].name : "Routeur";
            // Pour F1ATB (slot 0 sans device dédié), le nom peut venir de routers[0].name
            lv_obj_t *c = make_card(scr, dev_x, dev_y, dev_w, DEV_H, C_GREEN);
            make_label(c, title, &lv_font_montserrat_12, C_GREEN, LV_ALIGN_TOP_LEFT, 0, 0);
            lbl_rtr_power[i]    = make_label(c, "--", &lv_font_montserrat_14, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 20);
            lbl_rtr_duration[i] = make_label(c, "",   &lv_font_montserrat_12, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 42);
            lbl_rtr_kwh[i]      = make_label(c, "",   &lv_font_montserrat_12, C_MUTED, LV_ALIGN_TOP_LEFT, 0, 60);
            dev_x += dev_w + GAP;
        }
    }

    // ── Liseré indicateur (haut de l'écran, 6 px) ────────────────────────────
    ind_strip = lv_obj_create(scr);
    lv_obj_set_pos(ind_strip, 0, 0);
    lv_obj_set_size(ind_strip, 800, 6);
    lv_obj_set_style_bg_color(ind_strip, C_MUTED, 0);
    lv_obj_set_style_bg_opa(ind_strip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(ind_strip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(ind_strip, 0, 0);
    lv_obj_set_style_pad_all(ind_strip, 0, 0);
    lv_obj_clear_flag(ind_strip, LV_OBJ_FLAG_SCROLLABLE);

    // ── Barre IP en bas ────────────────────────────────────────────────────────
    lbl_ip_bar = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_ip_bar, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_ip_bar, C_MUTED, 0);
    lv_label_set_text(lbl_ip_bar, "Config : http://... (connexion WiFi en cours)");
    lv_obj_align(lbl_ip_bar, LV_ALIGN_BOTTOM_MID, 0, -4);

    Serial.printf("[ui] ui_create done — %d bats, %d rtrs, main_h=%d\n", n_bats, n_rtrs, main_h);
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

    // ── Barre de production solaire ───────────────────────────────────────────
    if (solar_bar) {
        int max_w = (g_cfg.solar_max_w > 0) ? g_cfg.solar_max_w : 1000;
        int val = (int)d.solar.power_w;
        if (val < 0) val = 0;
        if (val > max_w) val = max_w;
        lv_bar_set_range(solar_bar, 0, max_w);
        lv_bar_set_value(solar_bar, val, LV_ANIM_OFF);
        int pct = val * 100 / max_w;
        snprintf(buf, sizeof(buf), "%d%%", pct);
        lv_label_set_text(solar_bar_pct, buf);
        lv_color_t col = (pct >= 80) ? C_GREEN : C_SOLAR;
        lv_obj_set_style_bg_color(solar_bar, col, LV_PART_INDICATOR);
    }

    // ── Batteries ─────────────────────────────────────────────────────────────
    for (int i = 0; i < MAX_BATTERIES; i++) {
        if (!lbl_bat_power[i]) continue;
        const auto &bat = d.batteries[i];
        if (bat.online) {
            float pw = bat.power_w;
            fmt_power(buf, sizeof(buf), fabsf(pw));
            char pbuf[32];
            snprintf(pbuf, sizeof(pbuf), "%s%s", pw >= 0 ? "+" : "-", buf);
            lv_label_set_text(lbl_bat_power[i], pbuf);
            lv_obj_set_style_text_color(lbl_bat_power[i],
                pw >= 0 ? C_SOLAR : C_GREEN, 0);
            snprintf(buf, sizeof(buf), "SOC : %.0f%%", bat.soc_pct);
            lv_label_set_text(lbl_bat_soc[i], buf);
            lv_obj_set_style_text_color(lbl_bat_soc[i],
                bat.soc_pct > 20 ? C_GREEN : C_DANGER, 0);
        } else {
            lv_label_set_text(lbl_bat_power[i], "--");
            lv_obj_set_style_text_color(lbl_bat_power[i], C_MUTED, 0);
            lv_label_set_text(lbl_bat_soc[i], "SOC : --");
            lv_obj_set_style_text_color(lbl_bat_soc[i], C_MUTED, 0);
        }
    }

    // ── Routeurs ──────────────────────────────────────────────────────────────
    for (int i = 0; i < MAX_ROUTERS; i++) {
        if (!lbl_rtr_power[i]) continue;
        const auto &rtr = d.routers[i];
        if (rtr.online) {
            if (rtr.power_w > 0)
                fmt_power(buf, sizeof(buf), rtr.power_w);
            else if (rtr.triac_pct > 0)
                snprintf(buf, sizeof(buf), "%.0f%%", rtr.triac_pct);
            else
                strcpy(buf, "--");
            lv_obj_set_style_text_color(lbl_rtr_power[i], rtr.active ? C_SOLAR : C_GREEN, 0);
            lv_label_set_text(lbl_rtr_power[i], buf);

            if (lbl_rtr_duration[i]) {
                if (rtr.duration_h > 0) {
                    int hh = (int)rtr.duration_h;
                    int mm = (int)((rtr.duration_h - hh) * 60.0f + 0.5f);
                    if (rtr.triac_pct > 0)
                        snprintf(buf, sizeof(buf), "%02dh%02d  %.0f%%", hh, mm, rtr.triac_pct);
                    else
                        snprintf(buf, sizeof(buf), "%02dh%02d", hh, mm);
                } else if (rtr.triac_pct > 0) {
                    snprintf(buf, sizeof(buf), "%.0f%%", rtr.triac_pct);
                } else {
                    buf[0] = '\0';
                }
                lv_label_set_text(lbl_rtr_duration[i], buf);
            }

            if (lbl_rtr_kwh[i]) {
                if (rtr.today_kwh > 0)
                    snprintf(buf, sizeof(buf), "Auj : %.2f kWh", rtr.today_kwh);
                else
                    buf[0] = '\0';
                lv_label_set_text(lbl_rtr_kwh[i], buf);
            }
        } else {
            lv_label_set_text(lbl_rtr_power[i], "--");
            lv_obj_set_style_text_color(lbl_rtr_power[i], C_MUTED, 0);
            if (lbl_rtr_duration[i]) lv_label_set_text(lbl_rtr_duration[i], "");
            if (lbl_rtr_kwh[i])      lv_label_set_text(lbl_rtr_kwh[i], "");
        }
    }

    // ── Liseré indicateur ────────────────────────────────────────────────────
    if (ind_strip) {
        lv_color_t col = C_MUTED;
        if      (d.grid.online && d.grid.power_w < -10) col = C_GREEN;
        else if (d.grid.online && d.grid.power_w >  10) col = C_DANGER;
        lv_obj_set_style_bg_color(ind_strip, col, 0);
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
