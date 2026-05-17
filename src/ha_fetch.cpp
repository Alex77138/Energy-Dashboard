#include "ha_fetch.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

// GET http://{host}/api/states/{entity}
// Authorization: Bearer {token}
// Response: {"state":"123.4","attributes":{...}}
static float fetch_entity_state(const char *host, const char *token, const char *entity) {
    if (!host[0] || !token[0] || !entity[0]) return NAN;

    char url[192];
    // host peut contenir le port (ex: "192.168.1.10:8123")
    snprintf(url, sizeof(url), "http://%s/api/states/%s", host, entity);

    HTTPClient http;
    http.begin(url);
    http.addHeader("Authorization", String("Bearer ") + token);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(4000);

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[ha] %s → HTTP %d\n", entity, code);
        http.end();
        return NAN;
    }

    String body = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, body)) return NAN;

    const char *state = doc["state"] | "unavailable";
    if (!state || strcmp(state, "unavailable") == 0 || strcmp(state, "unknown") == 0)
        return NAN;

    return atof(state);
}

void ha_fetch_grid(GridData &out, const char *host, const char *token, const char *entity) {
    float v = fetch_entity_state(host, token, entity);
    if (isnan(v)) {
        out.online   = false;
        out.power_w  = 0;
    } else {
        out.online  = true;
        out.power_w = v;
    }
    out.today_kwh = 0;  // non fourni par une entité puissance unique
}

float ha_fetch_energy(const char *host, const char *token, const char *entity) {
    return fetch_entity_state(host, token, entity);
}

void ha_fetch_battery(BatteryData &out, const char *host, const char *token,
                      const char *power_entity, const char *soc_entity) {
    if (!host || !host[0] || !token || !token[0]) { out.online = false; return; }
    float power = NAN, soc = NAN;
    if (power_entity && power_entity[0]) power = fetch_entity_state(host, token, power_entity);
    if (soc_entity   && soc_entity[0])   soc   = fetch_entity_state(host, token, soc_entity);
    if (isnan(power) && isnan(soc)) { out.online = false; return; }
    out.online  = true;
    out.power_w = isnan(power) ? 0.0f : power;
    out.soc_pct = isnan(soc)   ? 0.0f : soc;
    Serial.printf("[ha-bat] power=%.1fW soc=%.0f%%\n", out.power_w, out.soc_pct);
}

void ha_fetch_router(RouterData &out, const char *host, const char *token,
                     const RouterConfig &rc) {
    if (!host || !host[0] || !token || !token[0]) { out.online = false; return; }
    float power = NAN;
    if (rc.power_entity[0]) power = fetch_entity_state(host, token, rc.power_entity);
    if (isnan(power)) { out.online = false; return; }
    out.online    = true;
    out.power_w   = power;
    out.forced    = false;

    if (rc.energy_entity[0]) {
        float e = fetch_entity_state(host, token, rc.energy_entity);
        if (!isnan(e)) out.today_kwh = e;
    }
    if (rc.active_entity[0]) {
        float a = fetch_entity_state(host, token, rc.active_entity);
        // binary_sensor/input_boolean: "on"=1, "off"=0; sensor numérique: >0.5
        // fetch_entity_state renvoie NAN pour "on"/"off" — on doit gérer via la string
        // mais fetch_entity_state fait atof(state), donc "on" → 0.0 et "off" → 0.0
        // Pour binary_sensor, on appelle directement l'API et teste la string
        // Contournement : "on" passe comme NAN via atof, on utilise un fetch dédié
        char url[192];
        snprintf(url, sizeof(url), "http://%s/api/states/%s", host, rc.active_entity);
        HTTPClient http;
        http.begin(url);
        http.addHeader("Authorization", String("Bearer ") + token);
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(4000);
        int code = http.GET();
        if (code == 200) {
            String body = http.getString();
            http.end();
            JsonDocument doc;
            if (!deserializeJson(doc, body)) {
                const char *st = doc["state"] | "off";
                out.active = (strcmp(st, "on") == 0 || strcmp(st, "1") == 0 || atof(st) > 0.5f);
            }
        } else { http.end(); }
    }
    if (rc.duration_entity[0]) {
        float d = fetch_entity_state(host, token, rc.duration_entity);
        if (!isnan(d)) out.duration_h = d;
    }
    if (rc.triac_entity[0]) {
        float t = fetch_entity_state(host, token, rc.triac_entity);
        if (!isnan(t)) out.triac_pct = t;
    }
    Serial.printf("[ha-rtr] power=%.1fW active=%d dur=%.2fh triac=%.0f%%\n",
                  out.power_w, out.active, out.duration_h, out.triac_pct);
}

void ha_fetch_solar(SolarData &out, const char *host, const char *token, const char *entity) {
    float v = fetch_entity_state(host, token, entity);
    if (isnan(v)) {
        out.online  = false;
        out.power_w = 0;
    } else {
        out.online  = true;
        out.power_w = v;
    }
    out.today_kwh  = 0;
    out.dc_voltage = 0;
    out.limit_pct  = 100;
}
