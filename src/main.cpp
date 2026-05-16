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

// ─── Sources qui fournissent déjà l'énergie journalière (pas cumulatif) ───────
static bool solar_provides_daily() {
    return g_cfg.solar_device == SolarDevice::OPENDTU  ||
           g_cfg.solar_device == SolarDevice::AHOYDTU  ||
           g_cfg.solar_device == SolarDevice::FRONIUS;
}

// ─── Tâche de polling (core 0) ────────────────────────────────────────────────
static void poll_task(void *) {
    // Baseline énergie journalière (corrige les sources cumulatives Shelly)
    static int   last_yday = -1;
    static float grid_base  = 0.0f;
    static float solar_base = 0.0f;

    for (;;) {
        if (WiFi.status() == WL_CONNECTED) {
            AppData tmp = {};

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
                    f1atb_fetch(g_cfg.grid_host, _rd, &tmp.grid);
                    break;
                }
                case GridDevice::HOME_ASSISTANT:
                    ha_fetch_grid(tmp.grid, g_cfg.grid_host,
                                  g_cfg.ha_token, g_cfg.grid_entity); break;
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
                                   g_cfg.ha_token, g_cfg.solar_entity); break;
                case SolarDevice::FRONIUS:
                    fronius_fetch(tmp.solar, g_cfg.solar_host); break;
                default: break;
            }

            if (g_cfg.battery_device == BatteryDevice::ESPHOME_JKBMS)
                esphome_jkbms_fetch(tmp.battery, g_cfg.battery_host);

            // ── Correction énergie journalière pour sources cumulatives ──────
            {
                struct tm ti;
                if (getLocalTime(&ti, 0)) {
                    if (ti.tm_yday != last_yday) {
                        // Nouveau jour (ou premier boot) : mémoriser le palier
                        last_yday  = ti.tm_yday;
                        grid_base  = tmp.grid.today_kwh;
                        solar_base = tmp.solar.today_kwh;
                    }
                }
                // Réseau : toujours cumulatif (Shelly/HA)
                float g_d = tmp.grid.today_kwh - grid_base;
                tmp.grid.today_kwh = (g_d > 0) ? g_d : 0;

                // Solaire : cumulatif sauf OpenDTU/AhoyDTU/Fronius (déjà journalier)
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
                g_data.grid    = tmp.grid;
                g_data.solar   = tmp.solar;
                g_data.battery = tmp.battery;
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
