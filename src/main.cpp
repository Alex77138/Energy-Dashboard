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

static bool      web_started     = false;
bool             ap_active       = false;
static uint32_t  last_ui_update  = 0;
static uint32_t  last_wifi_retry = 0;
static DNSServer dns_server;

static void ap_start() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("DashEnergy-Config", "");
    ap_active = true;
    // Portail captif : redirige toutes les requêtes DNS vers 192.168.4.1
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

// ─── Données fictives pour le mode démo ───────────────────────────────────────
// Bilan cohérent : réseau = charge_maison + routeur - solaire - décharge_bat
static void fill_demo_data(AppData &tmp) {
    float t = millis() / 1000.0f;

    // ── Solaire ──────────────────────────────────────────────────────────────
    float solar_w = 400.0f + 400.0f * sinf(t * 0.1f);  // 0–800 W, cycle ~63 s
    if (solar_w < 0) solar_w = 0;
    tmp.solar.online     = true;
    tmp.solar.power_w    = solar_w;
    tmp.solar.today_kwh  = 3.14f + t * 0.0001f;         // kWh cumulé journalier
    tmp.solar.dc_voltage = 37.5f + 2.0f * sinf(t * 0.07f);
    tmp.solar.limit_pct  = 100;

    // ── Charge maison (consommation de base, légèrement variable) ────────────
    float house_w = 450.0f + 120.0f * sinf(t * 0.04f + 0.8f);  // ~330–570 W
    float excess_w = solar_w - house_w;  // positif = surplus, négatif = déficit

    // ── Routeurs : dérivent le surplus solaire vers l'eau chaude ─────────────
    bool f1atb_slot0 = (g_cfg.grid_device == GridDevice::F1ATB &&
                        g_cfg.routers[0].device == RouterDevice::NONE);
    float total_rtr_w = 0;
    for (int i = 0; i < MAX_ROUTERS; i++) {
        if (g_cfg.routers[i].device == RouterDevice::NONE && !(i == 0 && f1atb_slot0)) continue;
        // Chaque routeur reçoit une fraction du surplus (divisé entre routeurs actifs)
        float alloc_w = fmaxf(0.0f, excess_w - total_rtr_w - 20.0f);  // seuil 20 W
        alloc_w = fminf(alloc_w, 800.0f);
        total_rtr_w += alloc_w;
        tmp.routers[i].online     = true;
        tmp.routers[i].power_w    = alloc_w;
        tmp.routers[i].active     = (alloc_w > 30.0f);
        tmp.routers[i].triac_pct  = fminf(100.0f, alloc_w / 8.0f);
        tmp.routers[i].duration_h = 1.25f + 0.75f * sinf(t * 0.03f + i * 0.5f);
        tmp.routers[i].today_kwh  = 0.85f + i * 0.3f + t * 0.00005f;
    }

    // ── Batteries : chargent sur le surplus résiduel, déchargent au déficit ──
    float remaining_excess = excess_w - total_rtr_w;  // surplus après routeurs
    float total_bat_w = 0;  // convention : + = décharge (apport), - = charge (prélèvement)
    for (int i = 0; i < MAX_BATTERIES; i++) {
        if (g_cfg.batteries[i].device == BatteryDevice::NONE) continue;
        float bat_w;
        if (remaining_excess > 10.0f)
            bat_w = -fminf(200.0f, remaining_excess / MAX_BATTERIES);  // charge
        else if (remaining_excess < -10.0f)
            bat_w = fminf(200.0f, -remaining_excess / MAX_BATTERIES);  // décharge
        else
            bat_w = 0;
        total_bat_w += bat_w;
        tmp.batteries[i].online  = true;
        tmp.batteries[i].power_w = bat_w;
        tmp.batteries[i].soc_pct = 65.0f + 20.0f * sinf(t * 0.015f + i * 0.8f);  // 45–85 %
    }

    // ── Réseau : bilan final (import + = réseau → maison, - = export) ────────
    // réseau = maison + routeur + charge_bat - solaire - décharge_bat
    //        = maison + routeur - solaire + total_bat_w (convention signed)
    //          (bat_w>0 = décharge = apport = réduit import)
    float grid_w = house_w + total_rtr_w - solar_w - total_bat_w;
    tmp.grid.online    = true;
    tmp.grid.power_w   = grid_w;
    tmp.grid.today_kwh = fmaxf(0.0f, 1.2f + 0.5f * sinf(t * 0.008f));  // ~0.7–1.7 kWh
}

// ─── Sources qui fournissent déjà l'énergie journalière (pas cumulatif) ───────
static bool solar_provides_daily() {
    return g_cfg.solar_device == SolarDevice::OPENDTU  ||
           g_cfg.solar_device == SolarDevice::AHOYDTU  ||
           g_cfg.solar_device == SolarDevice::FRONIUS;
    // Autres sources (Shelly, HA) → valeur cumulative → soustraction baseline nécessaire
}
static bool grid_provides_daily() {
    return false; // Toutes les sources réseau envoient le total cumulé
}

// ─── Tâche de polling (core 0) ────────────────────────────────────────────────
static void poll_task(void *) {
    // Baseline énergie journalière (corrige les sources cumulatives Shelly)
    static int   last_yday = -1;
    static float grid_base  = 0.0f;
    static float solar_base = 0.0f;

    for (;;) {
        if (WiFi.status() == WL_CONNECTED) {
            // ── Restauration au premier cycle après sync NTP ─────────────────
            static bool daily_loaded = false;
            if (!daily_loaded) {
                struct tm ti;
                if (getLocalTime(&ti, 0)) {
                    int saved_yday; float saved_gb, saved_sb;
                    if (sd_load_daily(&saved_yday, &saved_gb, &saved_sb) &&
                        saved_yday == ti.tm_yday) {
                        last_yday  = saved_yday;
                        grid_base  = saved_gb;
                        solar_base = saved_sb;
                        Serial.printf("[poll] Baselines restaurees J%d: reseau=%.3f solar=%.3f kWh\n",
                                      saved_yday, grid_base, solar_base);
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
                    memcpy(g_data.batteries, tmp.batteries, sizeof(tmp.batteries));
                    memcpy(g_data.routers,   tmp.routers,   sizeof(tmp.routers));
                    xSemaphoreGive(g_data.mutex);
                }
                webserver_log(tmp);
                vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
                continue;
            }

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
                    ha_fetch_grid(tmp.grid, g_cfg.grid_host,
                                  g_cfg.ha_token, g_cfg.grid_entity);
                    if (g_cfg.grid_energy_entity[0]) {
                        float e = ha_fetch_energy(g_cfg.grid_host, g_cfg.ha_token,
                                                  g_cfg.grid_energy_entity);
                        if (!isnan(e)) tmp.grid.today_kwh = e;
                    }
                    break;
                default: break;
            }

            switch (g_cfg.solar_device) {
                case SolarDevice::OPENDTU:
                    opendtu_fetch(tmp.solar, g_cfg.solar_host,
                                  g_cfg.solar_user, g_cfg.solar_pass,
                                  g_cfg.solar_serial); break;
                case SolarDevice::AHOYDTU:
                    ahoydtu_fetch(tmp.solar, g_cfg.solar_host,
                                  g_cfg.solar_serial); break;
                case SolarDevice::SHELLY_PLUG_G1:
                    shelly_plug_g1_fetch(tmp.solar, g_cfg.solar_host); break;
                case SolarDevice::SHELLY_PLUG_G2G3:
                    shelly_plug_g2g3_fetch(tmp.solar, g_cfg.solar_host); break;
                case SolarDevice::SHELLY_EM_1P:
                    shelly_em_fetch(tmp.solar, g_cfg.solar_host, 1); break;
                case SolarDevice::SHELLY_EM_2P:
                    shelly_em_fetch(tmp.solar, g_cfg.solar_host, 2); break;
                case SolarDevice::SHELLY_3EM_1P:
                    shelly_3em_fetch(tmp.solar, g_cfg.solar_host, 1); break;
                case SolarDevice::SHELLY_3EM_2P:
                    shelly_3em_fetch(tmp.solar, g_cfg.solar_host, 2); break;
                case SolarDevice::SHELLY_3EM_3P:
                    shelly_3em_fetch(tmp.solar, g_cfg.solar_host, 3); break;
                case SolarDevice::HOME_ASSISTANT:
                    ha_fetch_solar(tmp.solar, g_cfg.solar_host,
                                   g_cfg.ha_token, g_cfg.solar_entity);
                    if (g_cfg.solar_energy_entity[0]) {
                        float e = ha_fetch_energy(g_cfg.solar_host, g_cfg.ha_token,
                                                  g_cfg.solar_energy_entity);
                        if (!isnan(e)) tmp.solar.today_kwh = e;
                    }
                    break;
                case SolarDevice::FRONIUS:
                    fronius_fetch(tmp.solar, g_cfg.solar_host); break;
                default: break;
            }

            for (int i = 0; i < MAX_BATTERIES; i++) {
                auto &bc = g_cfg.batteries[i];
                auto &bd = tmp.batteries[i];
                switch (bc.device) {
                    case BatteryDevice::ESPHOME_JKBMS:
                        esphome_jkbms_fetch(bd, bc.host); break;
                    case BatteryDevice::HOME_ASSISTANT:
                        ha_fetch_battery(bd, bc.host, g_cfg.ha_token,
                                         bc.power_entity, bc.soc_entity); break;
                    default: break;
                }
            }

            for (int i = 0; i < MAX_ROUTERS; i++) {
                auto &rc = g_cfg.routers[i];
                auto &rd = tmp.routers[i];
                if (rc.device == RouterDevice::HOME_ASSISTANT) {
                    ha_fetch_router(rd, rc.host, g_cfg.ha_token, rc);
                }
            }

            // ── Correction énergie journalière pour sources cumulatives ──────
            {
                struct tm ti;
                if (getLocalTime(&ti, 0)) {
                    if (ti.tm_yday != last_yday) {
                        // Nouveau jour (ou premier boot) : mémoriser le palier
                        last_yday  = ti.tm_yday;
                        grid_base  = tmp.grid.today_kwh;
                        solar_base = tmp.solar.today_kwh;
                        sd_save_daily(last_yday, grid_base, solar_base);
                    }
                }
                // Réseau : cumulatif sauf si entité énergie HA configurée
                if (!grid_provides_daily()) {
                    float g_d = tmp.grid.today_kwh - grid_base;
                    tmp.grid.today_kwh = (g_d > 0) ? g_d : 0;
                }

                // Solaire : cumulatif sauf OpenDTU/AhoyDTU/Fronius/entité énergie HA
                if (!solar_provides_daily()) {
                    float s_d = tmp.solar.today_kwh - solar_base;
                    tmp.solar.today_kwh = (s_d > 0) ? s_d : 0;
                }
            }

            Serial.printf("[poll] Grid: online=%d %.0fW %.3fkWh  Solar: online=%d %.0fW %.3fkWh\n",
                          tmp.grid.online, tmp.grid.power_w, tmp.grid.today_kwh,
                          tmp.solar.online, tmp.solar.power_w, tmp.solar.today_kwh);

            tmp.mutex = g_data.mutex;
            if (xSemaphoreTake(g_data.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                g_data.grid  = tmp.grid;
                g_data.solar = tmp.solar;
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

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("\n[boot] Dash Energy " FIRMWARE_VERSION);

    device_config_load();
    Serial.printf("[boot] Grid=%d %s  Solar=%d %s  Rot=%d\n",
                  (int)g_cfg.grid_device,  g_cfg.grid_host,
                  (int)g_cfg.solar_device, g_cfg.solar_host,
                  g_cfg.display_rotation);

    // Fuseau horaire appliqué immédiatement (fix NTP en retard de 2h)
    setenv("TZ", g_cfg.timezone, 1);
    tzset();

    g_data.mutex = xSemaphoreCreateMutex();

    if (!display_init(g_cfg.display_rotation)) {
        Serial.println("[boot] ECHEC ecran — PSRAM?");
        while (true) delay(1000);
    }
    Serial.println("[boot] Ecran OK");

    ui_create();
    lv_refr_now(NULL);

    // IP fixe si configurée
    {
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
                gateway.fromString(gw.length() > 6 ? gw : ip.substring(0, ip.lastIndexOf('.') + 1) + "1");
                subnet.fromString(nm);
                dns_addr.fromString(dns);
                WiFi.config(addr, gateway, subnet, dns_addr);
                Serial.printf("[wifi] IP fixe: %s\n", ip.c_str());
            }
        }
        p.end();
    }

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
        display_tick();
        delay(10);
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

// ─── Loop (core 1 — LVGL) ────────────────────────────────────────────────────
void loop() {
    display_tick();

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

    if (now - last_ui_update >= 500) {
        last_ui_update = now;
        if (xSemaphoreTake(g_data.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            ui_update(g_data);
            xSemaphoreGive(g_data.mutex);
        }
        lv_refr_now(NULL);
    }
    delay(5);
}
