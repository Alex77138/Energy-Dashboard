// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Alexandre Richard -- https://github.com/Alex77138/Energy-Dashboard
#include "mqtt_pub.h"
#include "device_config.h"
#include "version.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

MQTTConfig g_mqtt;

static WiFiClient   s_wifi;
static PubSubClient s_client(s_wifi);
static uint32_t     s_last_reconnect = 0;
static bool         s_ha_announced   = false;

void mqtt_config_load() {
    Preferences p;
    p.begin("mqtt_cfg", true);
    g_mqtt.enabled      = p.getBool("enabled", false);
    String h = p.getString("host", "");  strncpy(g_mqtt.host,       h.c_str(), sizeof(g_mqtt.host)-1);
    g_mqtt.port         = p.getUShort("port", 1883);
    String u = p.getString("user", "");  strncpy(g_mqtt.user,       u.c_str(), sizeof(g_mqtt.user)-1);
    String pw= p.getString("pass", "");  strncpy(g_mqtt.pass,       pw.c_str(),sizeof(g_mqtt.pass)-1);
    String t = p.getString("topic","dashenergy"); strncpy(g_mqtt.base_topic, t.c_str(), sizeof(g_mqtt.base_topic)-1);
    g_mqtt.ha_discovery = p.getBool("ha_disc", true);
    p.end();
}

void mqtt_config_save(const MQTTConfig &cfg) {
    Preferences p;
    p.begin("mqtt_cfg", false);
    p.putBool  ("enabled", cfg.enabled);
    p.putString("host",    cfg.host);
    p.putUShort("port",    cfg.port);
    p.putString("user",    cfg.user);
    p.putString("pass",    cfg.pass);
    p.putString("topic",   cfg.base_topic);
    p.putBool  ("ha_disc", cfg.ha_discovery);
    p.end();
}

static void get_mac(char *buf, size_t sz) {
    uint64_t mac = ESP.getEfuseMac();
    snprintf(buf, sz, "%04X%08X", (uint16_t)(mac >> 32), (uint32_t)mac);
}

static void pub(const char *subtopic, const char *payload, bool retain = false) {
    char topic[80];
    snprintf(topic, sizeof(topic), "%s/%s", g_mqtt.base_topic, subtopic);
    s_client.publish(topic, payload, retain);
}

static void ha_sensor(const char *sid, const char *name,
                      const char *state_sub, const char *unit,
                      const char *dev_class, const char *state_class,
                      const char *mac) {
    char disc_topic[128];
    snprintf(disc_topic, sizeof(disc_topic),
             "homeassistant/sensor/%s_%s/config", g_mqtt.base_topic, sid);

    char state_full[80];
    snprintf(state_full, sizeof(state_full), "%s/%s", g_mqtt.base_topic, state_sub);

    JsonDocument doc;
    doc["name"]        = name;
    doc["state_topic"] = state_full;
    if (unit[0])       doc["unit_of_measurement"] = unit;
    if (dev_class[0])  doc["device_class"]        = dev_class;
    if (state_class[0])doc["state_class"]          = state_class;
    char uid[64];
    snprintf(uid, sizeof(uid), "%s_%s_%s", g_mqtt.base_topic, mac, sid);
    doc["unique_id"] = uid;

    auto dev = doc["device"].to<JsonObject>();
    char dev_id[24];
    snprintf(dev_id, sizeof(dev_id), "%s_%s", g_mqtt.base_topic, mac);
    dev["identifiers"][0] = dev_id;
    dev["name"]           = g_cfg.device_name;
    dev["model"]          = "DashEnergy JC8048W550";
    dev["sw_version"]     = FIRMWARE_VERSION;
    dev["manufacturer"]   = "Custom";

    char payload[700];
    serializeJson(doc, payload, sizeof(payload));
    s_client.publish(disc_topic, payload, true);
}

static void announce_ha() {
    char mac[13];
    get_mac(mac, sizeof(mac));
    ha_sensor("grid_power",      "Puissance reseau",    "grid/power_w",      "W",   "power",   "measurement",      mac);
    ha_sensor("grid_energy_day", "Energie reseau auj.", "grid/energy_kwh",   "kWh", "energy",  "total_increasing", mac);
    ha_sensor("grid_online",     "Reseau en ligne",     "grid/online",       "",    "",         "",                 mac);
    ha_sensor("solar_power",     "Puissance solaire",   "solar/power_w",     "W",   "power",   "measurement",      mac);
    ha_sensor("solar_energy_day","Energie solaire auj.","solar/energy_kwh",  "kWh", "energy",  "total_increasing", mac);
    ha_sensor("solar_dc_v",      "Tension DC onduleur", "solar/dc_voltage",  "V",   "voltage", "measurement",      mac);
    ha_sensor("solar_limit",     "Limite onduleur",     "solar/limit_pct",   "%",   "",        "measurement",      mac);
    ha_sensor("solar_online",    "Solaire en ligne",    "solar/online",      "",    "",         "",                 mac);
    ha_sensor("sys_rssi",        "WiFi RSSI",           "system/rssi",       "dBm", "signal_strength","measurement",mac);
    Serial.println("[mqtt] HA auto-discovery publie");
}

static bool reconnect() {
    uint32_t now = millis();
    if (now - s_last_reconnect < 30000) return false;
    s_last_reconnect = now;

    char cid[32];
    snprintf(cid, sizeof(cid), "dashenergy_%04X", (uint16_t)(now & 0xFFFF));
    bool ok = g_mqtt.user[0]
        ? s_client.connect(cid, g_mqtt.user, g_mqtt.pass)
        : s_client.connect(cid);

    if (ok) {
        Serial.printf("[mqtt] Connecte: %s:%d  topic=%s\n",
                      g_mqtt.host, g_mqtt.port, g_mqtt.base_topic);
        s_ha_announced = false;
    } else {
        Serial.printf("[mqtt] Echec rc=%d\n", s_client.state());
    }
    return ok;
}

void mqtt_init() {
    mqtt_config_load();
    if (!g_mqtt.enabled || !g_mqtt.host[0]) return;
    s_client.setServer(g_mqtt.host, g_mqtt.port);
    s_client.setBufferSize(750);
    Serial.printf("[mqtt] Init: %s:%d\n", g_mqtt.host, g_mqtt.port);
}

void mqtt_loop(const AppData &d) {
    if (!g_mqtt.enabled || !g_mqtt.host[0]) return;
    if (WiFi.status() != WL_CONNECTED) return;

    if (!s_client.connected() && !reconnect()) return;
    s_client.loop();

    if (!s_ha_announced && g_mqtt.ha_discovery) {
        announce_ha();
        s_ha_announced = true;
    }

    char v[24];
    snprintf(v, sizeof(v), "%.1f", d.grid.power_w);    pub("grid/power_w",    v);
    snprintf(v, sizeof(v), "%.3f", d.grid.today_kwh);  pub("grid/energy_kwh", v);
    pub("grid/online", d.grid.online ? "1" : "0");

    snprintf(v, sizeof(v), "%.1f", d.solar.power_w);   pub("solar/power_w",    v);
    snprintf(v, sizeof(v), "%.3f", d.solar.today_kwh); pub("solar/energy_kwh", v);
    snprintf(v, sizeof(v), "%.1f", d.solar.dc_voltage);pub("solar/dc_voltage", v);
    snprintf(v, sizeof(v), "%d",   d.solar.limit_pct); pub("solar/limit_pct",  v);
    pub("solar/online", d.solar.online ? "1" : "0");

    snprintf(v, sizeof(v), "%d", (int)WiFi.RSSI());    pub("system/rssi", v);
}
