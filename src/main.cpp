// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Alexandre Richard -- https://github.com/Alex77138/Energy-Dashboard
#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <time.h>
#include "config.h"
#include "types.h"
#include "device_config.h"
#include "display.h"
#include "ui.h"
#include "shelly.h"
#include "opendtu.h"
#include "f1atb.h"
#include "webserver.h"
#include "wifi_config.h"
#include "version.h"
#include "mqtt_pub.h"
#include "sd_logger.h"
#include "ha_fetch.h"
#include "fronius.h"
#include "battery.h"

AppData g_data;

static bool             web_started        = false;
bool                    ap_active          = false;
static uint32_t         last_wifi_retry    = 0;
static DNSServer        dns_server;
static SemaphoreHandle_t s_display_ready   = nullptr;

static void ap_start() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("DashEnergy-Config", "");
    ap_active = true;
    dns_server.start(53, "*", IPAddress(192, 168, 4, 1));
    Serial.println("[ap] DashEnergy-Config actif — http://192.168.4.1/");
}

static void ap_stop() {
    dns_server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    ap_active = false;
    Serial.println("[ap] AP desactive");
}

// ─── Calcul numéro de semaine ISO (Lun=1 … Dim=7) ────────────────────────────
static int iso_week_num(const struct tm &t) {
    char buf[4];
    strftime(buf, sizeof(buf), "%V", &t);
    return atoi(buf);
}

// ─── Mode démo ────────────────────────────────────────────────────────────────
static void fill_demo_data(AppData &tmp) {
    float t = millis() / 1000.0f;

    // Deux sources solaires animées indépendamment
    int n_solar = 0;
    for (int i = 0; i < MAX_SOLAR; i++)
        if (g_cfg.solars[i].device != SolarDevice::NONE) n_solar++;
    if (n_solar == 0) n_solar = 2;  // mode démo sans config : affiche 2 sources fictives

    float total_solar_w = 0;
    float total_solar_kwh = 0;
    for (int i = 0; i < MAX_SOLAR; i++) {
        auto &sd = tmp.solar_src[i];
        if (i >= n_solar) { sd = {}; continue; }
        float offset = i * 0.4f;
        float pw = fmaxf(0.0f, 400.0f + (300.0f - i * 80.0f) * sinf(t * 0.1f + offset));
        sd.online      = true;
        sd.power_w     = pw;
        sd.today_kwh   = 3.14f / (i + 1) + t * 0.00008f;
        sd.week_kwh    = sd.today_kwh * 5.2f;
        sd.month_kwh   = sd.today_kwh * 18.7f;
        sd.dc_voltage  = (i == 0) ? (37.5f + 2.0f * sinf(t * 0.07f)) : 0;
        sd.limit_pct   = 100;
        total_solar_w   += pw;
        total_solar_kwh += sd.today_kwh;
    }
    tmp.solar.online     = true;
    tmp.solar.power_w    = total_solar_w;
    tmp.solar.today_kwh  = total_solar_kwh;
    tmp.solar.week_kwh   = total_solar_kwh * 5.2f;
    tmp.solar.month_kwh  = total_solar_kwh * 18.7f;
    tmp.solar.dc_voltage = tmp.solar_src[0].dc_voltage;
    tmp.solar.limit_pct  = 100;

    float house_w = 450.0f + 120.0f * sinf(t * 0.04f + 0.8f);
    float excess_w = total_solar_w - house_w;

    bool f1atb_slot0 = (g_cfg.grid_device == GridDevice::F1ATB &&
                        g_cfg.routers[0].device == RouterDevice::NONE);
    float total_rtr_w = 0;
    for (int i = 0; i < MAX_ROUTERS; i++) {
        if (g_cfg.routers[i].device == RouterDevice::NONE && !(i == 0 && f1atb_slot0)) continue;
        float alloc_w = fmaxf(0.0f, excess_w - total_rtr_w - 20.0f);
        alloc_w = fminf(alloc_w, 800.0f);
        total_rtr_w += alloc_w;
        tmp.routers[i].online         = true;
        tmp.routers[i].power_w        = alloc_w;
        tmp.routers[i].voltage_v      = 230.0f + 2.0f * sinf(t * 0.03f);
        tmp.routers[i].current_a      = alloc_w > 0 ? alloc_w / tmp.routers[i].voltage_v : 0;
        tmp.routers[i].active         = (alloc_w > 30.0f);
        tmp.routers[i].triac_pct      = fminf(100.0f, alloc_w / 8.0f);
        tmp.routers[i].duration_h     = 1.25f + 0.75f * sinf(t * 0.03f + i * 0.5f);
        tmp.routers[i].today_kwh      = 0.85f + i * 0.3f + t * 0.00005f;
        tmp.routers[i].week_kwh       = tmp.routers[i].today_kwh * 4.8f;
        tmp.routers[i].month_kwh      = tmp.routers[i].today_kwh * 17.3f;
        tmp.routers[i].week_duration_h  = tmp.routers[i].duration_h * 4.8f;
        tmp.routers[i].month_duration_h = tmp.routers[i].duration_h * 17.3f;
    }

    float remaining_excess = excess_w - total_rtr_w;
    float total_bat_w = 0;
    for (int i = 0; i < MAX_BATTERIES; i++) {
        if (g_cfg.batteries[i].device == BatteryDevice::NONE) continue;
        float bat_w;
        if (remaining_excess > 10.0f)
            bat_w = -fminf(200.0f, remaining_excess / MAX_BATTERIES);
        else if (remaining_excess < -10.0f)
            bat_w = fminf(200.0f, -remaining_excess / MAX_BATTERIES);
        else
            bat_w = 0;
        total_bat_w += bat_w;
        tmp.batteries[i].online    = true;
        tmp.batteries[i].power_w   = bat_w;
        tmp.batteries[i].voltage_v = 48.0f + 2.0f * sinf(t * 0.02f + i * 0.4f);
        tmp.batteries[i].current_a = bat_w != 0 ? bat_w / tmp.batteries[i].voltage_v : 0;
        tmp.batteries[i].soc_pct   = 65.0f + 20.0f * sinf(t * 0.015f + i * 0.8f);
        tmp.batteries[i].today_kwh = 0.4f + i * 0.2f + t * 0.00003f;
        tmp.batteries[i].week_kwh  = tmp.batteries[i].today_kwh * 4.8f;
        tmp.batteries[i].month_kwh = tmp.batteries[i].today_kwh * 18.5f;
    }

    float grid_w = house_w + total_rtr_w - total_solar_w - total_bat_w;
    tmp.grid.online    = true;
    tmp.grid.power_w   = grid_w;
    tmp.grid.voltage_v = 230.0f + 3.0f * sinf(t * 0.05f);
    tmp.grid.current_a = fabsf(grid_w) / tmp.grid.voltage_v;
    tmp.grid.today_kwh = fmaxf(0.0f, 1.2f + 0.5f * sinf(t * 0.008f));
    tmp.grid.week_kwh  = tmp.grid.today_kwh * 5.1f;
    tmp.grid.month_kwh = tmp.grid.today_kwh * 19.2f;
}

// ─── Tâche display (core 0) ───────────────────────────────────────────────────
static void display_task(void *) {
    if (!display_init(g_cfg.display_rotation)) {
        Serial.println("[display] ECHEC — PSRAM?");
        xSemaphoreGive(s_display_ready);
        vTaskDelete(nullptr);
        return;
    }
    ui_create();
    lv_refr_now(NULL);
    xSemaphoreGive(s_display_ready);

    uint32_t last_ui_upd = 0;
    for (;;) {
        display_tick();
        ui_process_nav_request();
        ui_auto_return_check();
        uint32_t now = millis();
        if (now - last_ui_upd >= 500) {
            last_ui_upd = now;
            if (xSemaphoreTake(g_data.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                ui_update(g_data);
                xSemaphoreGive(g_data.mutex);
            }
            lv_refr_now(NULL);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ─── Tâche de polling (core 0) ────────────────────────────────────────────────
static void poll_task(void *) {
    static int   last_yday       = -1;
    static float grid_base       = 0.0f;
    static float solar_base[MAX_SOLAR] = {};
    static float router_base[MAX_ROUTERS] = {};
    static bool  router_base_set = false;
    static int32_t last_daily_save_ts = 0;

    // Bases hebdo/mensuelles
    static float grid_week_base  = 0.0f, grid_month_base  = 0.0f;
    static float sol_week_base[MAX_SOLAR]    = {}, sol_month_base[MAX_SOLAR]    = {};
    static float bat_week_base[MAX_BATTERIES]= {}, bat_month_base[MAX_BATTERIES]= {};
    static float rtr_week_base[MAX_ROUTERS]  = {}, rtr_month_base[MAX_ROUTERS]  = {};
    static float rtr_dur_week[MAX_ROUTERS]   = {}, rtr_dur_month[MAX_ROUTERS]   = {};
    static int   last_week_num   = -1, last_month_num = -1;

    for (;;) {
        if (WiFi.status() == WL_CONNECTED) {
            // ── Restauration bases au premier cycle après NTP sync ──────────
            static bool daily_loaded = false;
            if (!daily_loaded) {
                struct tm ti;
                if (getLocalTime(&ti, 0)) {
                    int saved_yday; float saved_gb;
                    float saved_sb[MAX_SOLAR] = {};
                    float saved_rb[MAX_ROUTERS] = {};
                    if (sd_load_daily(&saved_yday, &saved_gb, saved_sb, MAX_SOLAR,
                                      saved_rb, MAX_ROUTERS) &&
                        saved_yday == ti.tm_yday) {
                        last_yday = saved_yday;
                        grid_base = saved_gb;
                        memcpy(solar_base,  saved_sb, sizeof(solar_base));
                        memcpy(router_base, saved_rb, sizeof(router_base));
                        router_base_set = true;
                        Serial.printf("[poll] Baselines J%d: reseau=%.3f\n", saved_yday, grid_base);
                    }

                    // Restauration bases hebdo/mensuelles
                    PeriodData pd;
                    if (sd_load_period(pd)) {
                        char week_str[4];
                        strftime(week_str, sizeof(week_str), "%V", &ti);
                        int cur_week  = atoi(week_str);
                        int cur_month = ti.tm_mon;
                        int cur_year  = ti.tm_year;

                        if (pd.week_year == cur_year && pd.week_num == cur_week) {
                            grid_week_base = pd.grid_week;
                            memcpy(sol_week_base,  pd.solar_week,   sizeof(sol_week_base));
                            memcpy(bat_week_base,  pd.bat_week,     sizeof(bat_week_base));
                            memcpy(rtr_week_base,  pd.rtr_week,     sizeof(rtr_week_base));
                            memcpy(rtr_dur_week,   pd.rtr_dur_week, sizeof(rtr_dur_week));
                            last_week_num = cur_week;
                        }
                        if (pd.month_year == cur_year && pd.month_num == cur_month) {
                            grid_month_base = pd.grid_month;
                            memcpy(sol_month_base, pd.solar_month,  sizeof(sol_month_base));
                            memcpy(bat_month_base, pd.bat_month,    sizeof(bat_month_base));
                            memcpy(rtr_month_base, pd.rtr_month,    sizeof(rtr_month_base));
                            memcpy(rtr_dur_month,  pd.rtr_dur_month,sizeof(rtr_dur_month));
                            last_month_num = cur_month;
                        }
                        Serial.printf("[poll] Period: week=%d/%d month=%d/%d\n",
                            pd.week_year, pd.week_num, pd.month_year, pd.month_num);
                    }

                    webserver_restore_day_ring();
                    daily_loaded = true;
                }
            }

            AppData tmp = {};

            if (g_cfg.demo_mode) {
                fill_demo_data(tmp);
                tmp.mutex = g_data.mutex;
                if (xSemaphoreTake(g_data.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    g_data.grid  = tmp.grid;
                    g_data.solar = tmp.solar;
                    memcpy(g_data.solar_src, tmp.solar_src, sizeof(tmp.solar_src));
                    memcpy(g_data.batteries, tmp.batteries, sizeof(tmp.batteries));
                    memcpy(g_data.routers,   tmp.routers,   sizeof(tmp.routers));
                    xSemaphoreGive(g_data.mutex);
                }
                webserver_log(tmp);
                vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
                continue;
            }

            // ── Réseau ───────────────────────────────────────────────────────
            switch (g_cfg.grid_device) {
                case GridDevice::SHELLY_EM_1P:
                    shelly_em_fetch(tmp.grid, g_cfg.grid_host, 1); break;
                case GridDevice::SHELLY_EM_2P:
                    shelly_em_fetch(tmp.grid, g_cfg.grid_host, 2); break;
                case GridDevice::SHELLY_3EM_1P:
                    shelly_3em_fetch(tmp.grid, g_cfg.grid_host, 1); break;
                case GridDevice::SHELLY_3EM_2P:
                    shelly_3em_fetch(tmp.grid, g_cfg.grid_host, 2); break;
                case GridDevice::SHELLY_3EM_3P:
                    shelly_3em_fetch(tmp.grid, g_cfg.grid_host, 3); break;
                case GridDevice::SHELLY_PRO_EM:
                    shelly_pro_em_fetch(tmp.grid, g_cfg.grid_host, 2); break;
                case GridDevice::SHELLY_PRO_3EM:
                    shelly_pro_em_fetch(tmp.grid, g_cfg.grid_host, 3); break;
                case GridDevice::F1ATB: {
                    RouterData _rd;
                    if (f1atb_fetch(g_cfg.grid_host, _rd, &tmp.grid) &&
                        g_cfg.routers[0].device == RouterDevice::NONE) {
                        tmp.routers[0] = _rd;
                    }
                    break;
                }
                case GridDevice::HOME_ASSISTANT:
                    ha_fetch_grid(tmp.grid, g_cfg.grid_host, g_cfg.ha_token,
                                  g_cfg.grid_entity,
                                  g_cfg.grid_voltage_entity,
                                  g_cfg.grid_current_entity);
                    if (g_cfg.grid_energy_entity[0]) {
                        float e = ha_fetch_energy(g_cfg.grid_host, g_cfg.ha_token,
                                                  g_cfg.grid_energy_entity);
                        if (!isnan(e)) tmp.grid.today_kwh = e;
                    }
                    break;
                default: break;
            }

            // ── Sources solaires (multi-source) ──────────────────────────────
            tmp.solar = {};
            for (int i = 0; i < MAX_SOLAR; i++) {
                auto &sc = g_cfg.solars[i];
                auto &sd = tmp.solar_src[i];
                sd = {};
                if (sc.device == SolarDevice::NONE) continue;

                switch (sc.device) {
                    case SolarDevice::OPENDTU:
                        opendtu_fetch(sd, sc.host, sc.user, sc.pass, sc.serial); break;
                    case SolarDevice::AHOYDTU:
                        ahoydtu_fetch(sd, sc.host, sc.serial); break;
                    case SolarDevice::SHELLY_PLUG_G1:
                        shelly_plug_g1_fetch(sd, sc.host); break;
                    case SolarDevice::SHELLY_PLUG_G2G3:
                        shelly_plug_g2g3_fetch(sd, sc.host); break;
                    case SolarDevice::SHELLY_EM_1P:
                        shelly_em_fetch(sd, sc.host, 1); break;
                    case SolarDevice::SHELLY_EM_2P:
                        shelly_em_fetch(sd, sc.host, 2); break;
                    case SolarDevice::SHELLY_3EM_1P:
                        shelly_3em_fetch(sd, sc.host, 1); break;
                    case SolarDevice::SHELLY_3EM_2P:
                        shelly_3em_fetch(sd, sc.host, 2); break;
                    case SolarDevice::SHELLY_3EM_3P:
                        shelly_3em_fetch(sd, sc.host, 3); break;
                    case SolarDevice::HOME_ASSISTANT:
                        ha_fetch_solar(sd, sc.host, g_cfg.ha_token, sc.entity);
                        if (sc.energy_entity[0]) {
                            float e = ha_fetch_energy(sc.host, g_cfg.ha_token, sc.energy_entity);
                            if (!isnan(e)) sd.today_kwh = e;
                        }
                        break;
                    case SolarDevice::FRONIUS:
                        fronius_fetch(sd, sc.host); break;
                    default: break;
                }
                // Agréger dans le total
                tmp.solar.power_w   += sd.power_w;
                tmp.solar.today_kwh += sd.today_kwh;
                if (sd.online) tmp.solar.online = true;
                if (i == 0) {
                    tmp.solar.dc_voltage = sd.dc_voltage;
                    tmp.solar.limit_pct  = sd.limit_pct;
                }
            }

            // ── Batteries ────────────────────────────────────────────────────
            for (int i = 0; i < MAX_BATTERIES; i++) {
                auto &bc = g_cfg.batteries[i];
                auto &bd = tmp.batteries[i];
                switch (bc.device) {
                    case BatteryDevice::ESPHOME_JKBMS:
                        esphome_jkbms_fetch(bd, bc.host); break;
                    case BatteryDevice::HOME_ASSISTANT:
                        ha_fetch_battery(bd, g_cfg.ha_token, bc); break;
                    default: break;
                }
            }

            // ── Routeurs ─────────────────────────────────────────────────────
            for (int i = 0; i < MAX_ROUTERS; i++) {
                auto &rc = g_cfg.routers[i];
                if (rc.device == RouterDevice::HOME_ASSISTANT) {
                    ha_fetch_router(tmp.routers[i], rc.host, g_cfg.ha_token, rc);
                }
            }

            // ── Correction énergie journalière + accumulation hebdo/mensuelle ─
            {
                struct tm ti;
                if (getLocalTime(&ti, 0)) {
                    char week_str[4];
                    strftime(week_str, sizeof(week_str), "%V", &ti);
                    int cur_week  = atoi(week_str);
                    int cur_month = ti.tm_mon;
                    int cur_year  = ti.tm_year;

                    if (ti.tm_yday != last_yday) {
                        if (last_yday != -1) {
                            // Capturer l'énergie d'hier AVANT de mettre à jour la baseline
                            float yest_grid = fmaxf(0.0f, tmp.grid.today_kwh - grid_base);
                            float yest_solar[MAX_SOLAR] = {};
                            for (int i = 0; i < MAX_SOLAR; i++)
                                yest_solar[i] = fmaxf(0.0f, tmp.solar_src[i].today_kwh - solar_base[i]);

                            bool week_changed  = (last_week_num  != -1 && cur_week  != last_week_num);
                            bool month_changed = (last_month_num != -1 && cur_month != last_month_num);

                            if (week_changed) {
                                grid_week_base = 0;
                                memset(sol_week_base,  0, sizeof(sol_week_base));
                                memset(bat_week_base,  0, sizeof(bat_week_base));
                                memset(rtr_week_base,  0, sizeof(rtr_week_base));
                                memset(rtr_dur_week,   0, sizeof(rtr_dur_week));
                            } else {
                                grid_week_base += yest_grid;
                                for (int i = 0; i < MAX_SOLAR; i++) sol_week_base[i] += yest_solar[i];
                                for (int i = 0; i < MAX_BATTERIES; i++) bat_week_base[i] += tmp.batteries[i].today_kwh;
                                for (int i = 0; i < MAX_ROUTERS; i++) {
                                    rtr_week_base[i] += tmp.routers[i].today_kwh;
                                    rtr_dur_week[i]  += tmp.routers[i].duration_h;
                                }
                            }

                            if (month_changed) {
                                grid_month_base = 0;
                                memset(sol_month_base,  0, sizeof(sol_month_base));
                                memset(bat_month_base,  0, sizeof(bat_month_base));
                                memset(rtr_month_base,  0, sizeof(rtr_month_base));
                                memset(rtr_dur_month,   0, sizeof(rtr_dur_month));
                            } else {
                                grid_month_base += yest_grid;
                                for (int i = 0; i < MAX_SOLAR; i++) sol_month_base[i] += yest_solar[i];
                                for (int i = 0; i < MAX_BATTERIES; i++) bat_month_base[i] += tmp.batteries[i].today_kwh;
                                for (int i = 0; i < MAX_ROUTERS; i++) {
                                    rtr_month_base[i] += tmp.routers[i].today_kwh;
                                    rtr_dur_month[i]  += tmp.routers[i].duration_h;
                                }
                            }

                            last_week_num  = cur_week;
                            last_month_num = cur_month;

                            PeriodData pd;
                            pd.week_year  = cur_year;  pd.week_num   = cur_week;
                            pd.month_year = cur_year;  pd.month_num  = cur_month;
                            pd.grid_week  = grid_week_base; pd.grid_month = grid_month_base;
                            memcpy(pd.solar_week,   sol_week_base,  sizeof(sol_week_base));
                            memcpy(pd.solar_month,  sol_month_base, sizeof(sol_month_base));
                            memcpy(pd.bat_week,     bat_week_base,  sizeof(bat_week_base));
                            memcpy(pd.bat_month,    bat_month_base, sizeof(bat_month_base));
                            memcpy(pd.rtr_week,     rtr_week_base,  sizeof(rtr_week_base));
                            memcpy(pd.rtr_month,    rtr_month_base, sizeof(rtr_month_base));
                            memcpy(pd.rtr_dur_week,  rtr_dur_week,  sizeof(rtr_dur_week));
                            memcpy(pd.rtr_dur_month, rtr_dur_month, sizeof(rtr_dur_month));
                            sd_save_period(pd);
                        }

                        // Nouveau jour : mettre à jour les baselines journalières
                        last_yday = ti.tm_yday;
                        grid_base = tmp.grid.today_kwh;
                        for (int i = 0; i < MAX_SOLAR; i++)
                            solar_base[i] = tmp.solar_src[i].today_kwh;
                        for (int i = 0; i < MAX_ROUTERS; i++)
                            router_base[i] = tmp.routers[i].today_kwh;
                        router_base_set = true;
                        if (last_week_num  == -1) last_week_num  = cur_week;
                        if (last_month_num == -1) last_month_num = cur_month;
                        sd_save_daily(last_yday, grid_base, solar_base, MAX_SOLAR,
                                      router_base, MAX_ROUTERS);
                        last_daily_save_ts = (int32_t)time(nullptr);
                    }
                    if (!router_base_set) {
                        for (int i = 0; i < MAX_ROUTERS; i++)
                            router_base[i] = tmp.routers[i].today_kwh;
                        router_base_set = true;
                    }

                    // Sauvegarde périodique des baselines (toutes les 5 min)
                    int32_t now_ts = (int32_t)time(nullptr);
                    if (now_ts > 0 && last_yday != -1 && now_ts - last_daily_save_ts >= 300) {
                        sd_save_daily(last_yday, grid_base, solar_base, MAX_SOLAR,
                                      router_base, MAX_ROUTERS);
                        last_daily_save_ts = now_ts;
                    }
                }

                // Correction cumulatif → journalier
                {
                    float g_d = tmp.grid.today_kwh - grid_base;
                    tmp.grid.today_kwh = fmaxf(0.0f, g_d);
                }
                for (int i = 0; i < MAX_SOLAR; i++) {
                    float s_d = tmp.solar_src[i].today_kwh - solar_base[i];
                    tmp.solar_src[i].today_kwh = fmaxf(0.0f, s_d);
                }
                for (int i = 0; i < MAX_ROUTERS; i++) {
                    float r_d = tmp.routers[i].today_kwh - router_base[i];
                    tmp.routers[i].today_kwh = fmaxf(0.0f, r_d);
                }

                // Recalculer le total solaire après correction
                float total_solar_kwh = 0;
                for (int i = 0; i < MAX_SOLAR; i++)
                    total_solar_kwh += tmp.solar_src[i].today_kwh;
                tmp.solar.today_kwh = total_solar_kwh;

                // Hebdo/mensuel live = base + aujourd'hui
                tmp.grid.week_kwh  = grid_week_base  + tmp.grid.today_kwh;
                tmp.grid.month_kwh = grid_month_base + tmp.grid.today_kwh;
                for (int i = 0; i < MAX_SOLAR; i++) {
                    tmp.solar_src[i].week_kwh  = sol_week_base[i]  + tmp.solar_src[i].today_kwh;
                    tmp.solar_src[i].month_kwh = sol_month_base[i] + tmp.solar_src[i].today_kwh;
                }
                for (int i = 0; i < MAX_BATTERIES; i++) {
                    tmp.batteries[i].week_kwh  = bat_week_base[i]  + tmp.batteries[i].today_kwh;
                    tmp.batteries[i].month_kwh = bat_month_base[i] + tmp.batteries[i].today_kwh;
                }
                for (int i = 0; i < MAX_ROUTERS; i++) {
                    tmp.routers[i].week_kwh         = rtr_week_base[i]  + tmp.routers[i].today_kwh;
                    tmp.routers[i].month_kwh        = rtr_month_base[i] + tmp.routers[i].today_kwh;
                    tmp.routers[i].week_duration_h  = rtr_dur_week[i]   + tmp.routers[i].duration_h;
                    tmp.routers[i].month_duration_h = rtr_dur_month[i]  + tmp.routers[i].duration_h;
                }
                // Total solaire hebdo/mensuel
                float sol_week = 0, sol_month = 0;
                for (int i = 0; i < MAX_SOLAR; i++) {
                    sol_week  += tmp.solar_src[i].week_kwh;
                    sol_month += tmp.solar_src[i].month_kwh;
                }
                tmp.solar.week_kwh  = sol_week;
                tmp.solar.month_kwh = sol_month;
            }

            Serial.printf("[poll] Grid: %d %.0fW %.3fkWh  Solar: %d %.0fW %.3fkWh  W:%.2f M:%.2f\n",
                          tmp.grid.online, tmp.grid.power_w, tmp.grid.today_kwh,
                          tmp.solar.online, tmp.solar.power_w, tmp.solar.today_kwh,
                          tmp.solar.week_kwh, tmp.solar.month_kwh);

            tmp.mutex = g_data.mutex;
            if (xSemaphoreTake(g_data.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                g_data.grid  = tmp.grid;
                g_data.solar = tmp.solar;
                memcpy(g_data.solar_src, tmp.solar_src, sizeof(tmp.solar_src));
                memcpy(g_data.batteries, tmp.batteries, sizeof(tmp.batteries));
                memcpy(g_data.routers,   tmp.routers,   sizeof(tmp.routers));
                xSemaphoreGive(g_data.mutex);
            }

            mqtt_loop(tmp);
            sd_log(tmp);
            webserver_log(tmp);
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

static void wifi_apply_static_config() {
    Preferences p;
    p.begin("wifi_net", true);
    bool dhcp = p.getBool("dhcp", true);
    if (!dhcp) {
        String ip  = p.getString("static_ip", "");
        String gw  = p.getString("static_gw", "");
        String nm  = p.getString("static_nm",  "255.255.255.0");
        String dns = p.getString("static_dns",  "8.8.8.8");
        if (ip.length() > 6) {
            IPAddress addr, gateway, subnet, dns_addr;
            addr.fromString(ip);
            gateway.fromString(gw.length() > 6 ? gw
                : ip.substring(0, ip.lastIndexOf('.') + 1) + "1");
            subnet.fromString(nm);
            dns_addr.fromString(dns);
            WiFi.config(addr, gateway, subnet, dns_addr);
            Serial.printf("[wifi] IP fixe: %s\n", ip.c_str());
        }
    }
    p.end();
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n[boot] Dash Energy " FIRMWARE_VERSION);

    device_config_load();
    int n_solar = 0;
    for (int i = 0; i < MAX_SOLAR; i++)
        if (g_cfg.solars[i].device != SolarDevice::NONE) n_solar++;
    Serial.printf("[boot] Grid=%d %s  Solar=%d sources  Rot=%d\n",
                  (int)g_cfg.grid_device, g_cfg.grid_host,
                  n_solar, g_cfg.display_rotation);

    setenv("TZ", g_cfg.timezone, 1);
    tzset();

    g_data.mutex     = xSemaphoreCreateMutex();
    s_display_ready  = xSemaphoreCreateBinary();

    xTaskCreatePinnedToCore(display_task, "display", 16384, nullptr, 2, nullptr, 0);

    if (xSemaphoreTake(s_display_ready, pdMS_TO_TICKS(5000)) != pdTRUE) {
        Serial.println("[boot] Timeout ecran");
    }
    Serial.println("[boot] Ecran OK");

    wifi_apply_static_config();
    ap_start();

    {
        String ssid, pass;
        if (wifi_config_load(ssid, pass)) {
            Serial.printf("[wifi] NVS: %s\n", ssid.c_str());
            WiFi.begin(ssid.c_str(), pass.c_str());
        } else {
            Serial.printf("[wifi] config.h: %s\n", WIFI_SSID);
            WiFi.begin(WIFI_SSID, WIFI_PASS);
        }
    }

    Serial.print("[wifi] Connexion");
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(" OK — " + WiFi.localIP().toString());
        ap_stop();
        configTzTime(g_cfg.timezone, "pool.ntp.org", "time.google.com");
        Serial.printf("[ntp] TZ=%s — sync en cours...\n", g_cfg.timezone);
    } else {
        Serial.println(" ECHEC — AP actif, reconnexion auto 30s");
    }

    sd_init();
    mqtt_init();
    webserver_start();
    web_started = true;

    xTaskCreatePinnedToCore(poll_task, "poll", 20480, nullptr, 1, nullptr, 0);
}

void loop() {
    uint32_t now = millis();

    if (ap_active) dns_server.processNextRequest();

    if (WiFi.status() != WL_CONNECTED) {
        if (!ap_active) ap_start();
        if (now - last_wifi_retry > 30000) {
            last_wifi_retry = now;
            WiFi.reconnect();
        }
    } else {
        if (ap_active) {
            ap_stop();
            configTzTime(g_cfg.timezone, "pool.ntp.org", "time.google.com");
        }
    }

    delay(100);
}
