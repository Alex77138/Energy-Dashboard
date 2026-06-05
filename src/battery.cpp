// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Alexandre Richard -- https://github.com/Alex77138/Energy-Dashboard
#include "battery.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ESPHome web_server expose les capteurs à : GET http://{host}/sensor/{sensor_id}
// Réponse : {"id":"sensor-xxx","state":"250.3 W","value":250.3}
// Projet : https://github.com/syssi/esphome-jk-bms

static float esphome_sensor(const char *host, const char *sensor_id) {
    char url[128];
    snprintf(url, sizeof(url), "http://%s/sensor/%s", host, sensor_id);
    HTTPClient http;
    http.begin(url);
    http.setTimeout(3000);
    int code = http.GET();
    if (code != 200) { http.end(); return NAN; }
    String body = http.getString();
    http.end();
    JsonDocument doc;
    if (deserializeJson(doc, body)) return NAN;
    // Gère la réponse directe {"value":...} et le tableau [{"value":...}]
    JsonVariant val = doc.is<JsonArray>() ? doc[0]["value"].as<JsonVariant>()
                                          : doc["value"].as<JsonVariant>();
    if (val.isNull()) return NAN;
    return val.as<float>();
}

bool esphome_jkbms_fetch(BatteryData &out, const char *host) {
    if (!host || !host[0]) { out.online = false; return false; }

    // power : positif = décharge (batterie fournit), négatif = charge
    float power = esphome_sensor(host, "power");
    float soc   = esphome_sensor(host, "state_of_charge");

    if (isnan(power) && isnan(soc)) {
        out.online = false;
        return false;
    }
    out.online  = true;
    out.power_w = isnan(power) ? 0.0f : power;
    out.soc_pct = isnan(soc)   ? 0.0f : soc;
    Serial.printf("[jkbms] power=%.1fW soc=%.0f%%\n", out.power_w, out.soc_pct);
    return true;
}
