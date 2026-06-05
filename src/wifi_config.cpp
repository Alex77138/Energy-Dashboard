// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Alexandre Richard -- https://github.com/Alex77138/Energy-Dashboard
#include "wifi_config.h"
#include "display.h"
#include <WiFi.h>
#include <Preferences.h>
#include <lvgl.h>

// ─── NVS ─────────────────────────────────────────────────────────────────────
static const char *NVS_NS   = "wifi_cfg";
static const char *NVS_SSID = "ssid";
static const char *NVS_PASS = "pass";

bool wifi_config_load(String &ssid, String &pass) {
    Preferences prefs;
    prefs.begin(NVS_NS, true);
    ssid = prefs.getString(NVS_SSID, "");
    pass = prefs.getString(NVS_PASS, "");
    prefs.end();
    return ssid.length() > 0;
}

static void save_credentials(const char *ssid, const char *pass) {
    Preferences prefs;
    prefs.begin(NVS_NS, false);
    prefs.putString(NVS_SSID, ssid);
    prefs.putString(NVS_PASS, pass);
    prefs.end();
}

// ─── Palette (cohérente avec ui.cpp) ─────────────────────────────────────────
#define C_BG     lv_color_hex(0x0D1117)
#define C_CARD   lv_color_hex(0x161B22)
#define C_ACCENT lv_color_hex(0x58A6FF)
#define C_GREEN  lv_color_hex(0x238636)
#define C_TEXT   lv_color_hex(0xE6EDF3)
#define C_MUTED  lv_color_hex(0x8B949E)
#define C_BORDER lv_color_hex(0x30363D)

// ─── État interne ─────────────────────────────────────────────────────────────
static lv_obj_t *g_list;
static lv_obj_t *g_ta_pass;
static lv_obj_t *g_lbl_selected;
static lv_obj_t *g_lbl_status;
static lv_obj_t *g_kb;

static char g_ssid[64] = "";
static char g_pass[64] = "";
static bool g_connected = false;

static enum { IDLE, SCANNING, CONNECTING } g_state = IDLE;
static unsigned long g_connect_start = 0;

// ─── Traitement résultats scan ────────────────────────────────────────────────
static void process_scan(int n) {
    lv_obj_clean(g_list);
    if (n <= 0) {
        lv_label_set_text(g_lbl_status, "Aucun réseau trouvé — réessaie");
        WiFi.scanDelete();
        return;
    }
    for (int i = 0; i < n && i < 20; i++) {
        String name = WiFi.SSID(i);
        if (name.length() == 0) continue;
        bool secured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        String label = name + (secured ? " " LV_SYMBOL_WARNING : "");
        lv_obj_t *btn = lv_list_add_btn(g_list, LV_SYMBOL_WIFI, label.c_str());
        lv_obj_set_style_bg_color(btn, C_CARD, 0);
        // stocker le SSID seul (sans l'icône cadenas) dans le user_data
        char *stored = (char *)malloc(name.length() + 1);
        if (stored) {
            strcpy(stored, name.c_str());
            lv_obj_set_user_data(btn, stored);
        }
        lv_obj_add_event_cb(btn, [](lv_event_t *e) {
            lv_obj_t *btn = lv_event_get_target(e);
            const char *name = (const char *)lv_obj_get_user_data(btn);
            if (!name) return;
            strncpy(g_ssid, name, sizeof(g_ssid) - 1);
            lv_label_set_text(g_lbl_selected, g_ssid);
            lv_textarea_set_text(g_ta_pass, "");
            lv_obj_clear_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
            lv_keyboard_set_textarea(g_kb, g_ta_pass);
        }, LV_EVENT_CLICKED, nullptr);
    }
    char buf[40];
    snprintf(buf, sizeof(buf), "%d réseau(x) — appuie pour sélectionner", n);
    lv_label_set_text(g_lbl_status, buf);
    WiFi.scanDelete();
}

// ─── Construction de l'écran ──────────────────────────────────────────────────
void wifi_config_run() {
    g_connected = false;
    g_ssid[0]   = '\0';
    g_pass[0]   = '\0';
    g_state     = IDLE;

    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_scr_load(scr);

    // ── Titre ──────────────────────────────────────────────────────────────────
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, LV_SYMBOL_WIFI "  Configuration WiFi");
    lv_obj_set_style_text_color(title, C_ACCENT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(title, 10, 10);

    // ── Bouton Scanner ─────────────────────────────────────────────────────────
    lv_obj_t *btn_scan = lv_btn_create(scr);
    lv_obj_set_pos(btn_scan, 644, 4);
    lv_obj_set_size(btn_scan, 150, 34);
    lv_obj_set_style_bg_color(btn_scan, C_GREEN, 0);
    lv_obj_set_style_bg_color(btn_scan, lv_color_hex(0x196127), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_scan, 8, 0);
    lv_obj_set_style_shadow_width(btn_scan, 0, 0);
    lv_obj_add_event_cb(btn_scan, [](lv_event_t *) {
        if (g_state != IDLE) return;
        g_state = SCANNING;
        lv_obj_clean(g_list);
        lv_label_set_text(g_lbl_status, "Scan en cours...");
        WiFi.scanNetworks(true); // asynchrone
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *l = lv_label_create(btn_scan);
    lv_label_set_text(l, LV_SYMBOL_REFRESH "  Scanner");
    lv_obj_set_style_text_color(l, C_TEXT, 0);
    lv_obj_center(l);

    // ── Liste des réseaux (gauche) ─────────────────────────────────────────────
    g_list = lv_list_create(scr);
    lv_obj_set_pos(g_list, 4, 44);
    lv_obj_set_size(g_list, 390, 210);
    lv_obj_set_style_bg_color(g_list, C_CARD, 0);
    lv_obj_set_style_border_color(g_list, C_BORDER, 0);
    lv_obj_set_style_border_width(g_list, 1, 0);
    lv_obj_set_style_radius(g_list, 8, 0);

    // ── Panneau droite ─────────────────────────────────────────────────────────
    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_set_pos(panel, 402, 44);
    lv_obj_set_size(panel, 394, 210);
    lv_obj_set_style_bg_color(panel, C_CARD, 0);
    lv_obj_set_style_border_color(panel, C_BORDER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 14, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_t = lv_label_create(panel);
    lv_label_set_text(lbl_t, "Réseau sélectionné :");
    lv_obj_set_style_text_color(lbl_t, C_MUTED, 0);
    lv_obj_set_style_text_font(lbl_t, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_t, LV_ALIGN_TOP_LEFT, 0, 0);

    g_lbl_selected = lv_label_create(panel);
    lv_label_set_text(g_lbl_selected, "—");
    lv_obj_set_style_text_color(g_lbl_selected, C_TEXT, 0);
    lv_obj_set_style_text_font(g_lbl_selected, &lv_font_montserrat_16, 0);
    lv_obj_set_width(g_lbl_selected, 366);
    lv_label_set_long_mode(g_lbl_selected, LV_LABEL_LONG_DOT);
    lv_obj_align(g_lbl_selected, LV_ALIGN_TOP_LEFT, 0, 22);

    lv_obj_t *lbl_p = lv_label_create(panel);
    lv_label_set_text(lbl_p, "Mot de passe :");
    lv_obj_set_style_text_color(lbl_p, C_MUTED, 0);
    lv_obj_set_style_text_font(lbl_p, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_p, LV_ALIGN_TOP_LEFT, 0, 54);

    g_ta_pass = lv_textarea_create(panel);
    lv_obj_align(g_ta_pass, LV_ALIGN_TOP_LEFT, 0, 76);
    lv_obj_set_size(g_ta_pass, 366, 44);
    lv_textarea_set_one_line(g_ta_pass, true);
    lv_textarea_set_password_mode(g_ta_pass, true);
    lv_textarea_set_placeholder_text(g_ta_pass, "Touche pour saisir...");
    lv_obj_set_style_bg_color(g_ta_pass, C_BG, 0);
    lv_obj_set_style_text_color(g_ta_pass, C_TEXT, 0);
    lv_obj_set_style_border_color(g_ta_pass, C_ACCENT, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(g_ta_pass, [](lv_event_t *) {
        lv_keyboard_set_textarea(g_kb, g_ta_pass);
        lv_obj_clear_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_FOCUSED, nullptr);

    // Bouton Connecter
    lv_obj_t *btn_conn = lv_btn_create(panel);
    lv_obj_align(btn_conn, LV_ALIGN_TOP_LEFT, 0, 134);
    lv_obj_set_size(btn_conn, 180, 44);
    lv_obj_set_style_bg_color(btn_conn, lv_color_hex(0x1F6FEB), 0);
    lv_obj_set_style_bg_color(btn_conn, lv_color_hex(0x0D5BBB), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_conn, 8, 0);
    lv_obj_set_style_shadow_width(btn_conn, 0, 0);
    lv_obj_add_event_cb(btn_conn, [](lv_event_t *) {
        if (g_state != IDLE) return;
        if (g_ssid[0] == '\0') {
            lv_label_set_text(g_lbl_status, "Sélectionne un réseau d'abord");
            return;
        }
        strncpy(g_pass, lv_textarea_get_text(g_ta_pass), sizeof(g_pass) - 1);
        lv_obj_add_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(g_lbl_status, "Connexion en cours...");
        WiFi.disconnect();
        WiFi.begin(g_ssid, g_pass);
        g_connect_start = millis();
        g_state = CONNECTING;
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl_conn = lv_label_create(btn_conn);
    lv_label_set_text(lbl_conn, LV_SYMBOL_OK "  Connecter");
    lv_obj_set_style_text_color(lbl_conn, C_TEXT, 0);
    lv_obj_center(lbl_conn);

    // ── Statut ─────────────────────────────────────────────────────────────────
    g_lbl_status = lv_label_create(scr);
    lv_label_set_text(g_lbl_status, "Appuie sur 'Scanner' pour voir les réseaux disponibles");
    lv_obj_set_style_text_color(g_lbl_status, C_MUTED, 0);
    lv_obj_set_style_text_font(g_lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(g_lbl_status, 10, 260);
    lv_obj_set_width(g_lbl_status, 780);
    lv_label_set_long_mode(g_lbl_status, LV_LABEL_LONG_WRAP);

    // ── Clavier tactile (caché par défaut, ancré en bas) ──────────────────────
    g_kb = lv_keyboard_create(scr);
    lv_obj_set_size(g_kb, 800, 210);
    lv_obj_align(g_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(g_kb, g_ta_pass);
    lv_obj_add_event_cb(g_kb, [](lv_event_t *) {
        lv_obj_add_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_READY, nullptr);

    // ── Scan automatique au démarrage ─────────────────────────────────────────
    g_state = SCANNING;
    WiFi.scanNetworks(true);
    lv_label_set_text(g_lbl_status, "Scan en cours...");

    // ── Boucle principale (bloque jusqu'à connexion) ───────────────────────────
    while (!g_connected) {
        display_tick();

        if (g_state == SCANNING) {
            int n = WiFi.scanComplete();
            if (n >= 0) {
                g_state = IDLE;
                process_scan(n);
            }
        } else if (g_state == CONNECTING) {
            if (WiFi.status() == WL_CONNECTED) {
                save_credentials(g_ssid, g_pass);
                String msg = String(LV_SYMBOL_OK) + "  Connecté ! IP : " + WiFi.localIP().toString();
                lv_label_set_text(g_lbl_status, msg.c_str());
                lv_obj_set_style_text_color(g_lbl_status, lv_color_hex(0x3FB950), 0);
                g_connected = true;
            } else if (millis() - g_connect_start > 15000) {
                WiFi.disconnect();
                lv_label_set_text(g_lbl_status, LV_SYMBOL_CLOSE "  Échec — mot de passe incorrect ?");
                lv_obj_set_style_text_color(g_lbl_status, lv_color_hex(0xF85149), 0);
                g_state = IDLE;
            }
        }
        delay(5);
    }

    delay(1500); // laisse le temps de lire "Connecté"
    lv_scr_load(lv_obj_create(nullptr)); // écran vide temporaire (main.cpp recrée l'UI)
    lv_obj_del(scr);
}
