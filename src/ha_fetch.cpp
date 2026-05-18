#include "ha_fetch.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

static float fetch_entity_state(const char *host, const char *token, const char *entity) {
    if (!host || !host[0] || !token || !token[0] || !entity || !entity[0]) return NAN;

    char url[192];
    snprintf(url, sizeof(url), "http://%s/api/states/%s", host, entity);

    HTTPClient http;
    http.begin(url);
    http.addHeader("Authorization", String("Bearer ") + token);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(4000);

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[ha] %s -> HTTP %d\n", entity, code);
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

void ha_fetch_grid(GridData &out, const char *host, const char *token,
                   const char *entity, const char *volt_ent, const char *cur_ent) {
    float v = fetch_entity_state(host, token, entity);
    if (isnan(v)) {
        out.online  = false;
        out.power_w = 0;
    } else {
        out.online  = true;
        out.power_w = v;
    }
    out.today_kwh = 0;
    if (volt_ent && volt_ent[0]) {
        float vv = fetch_entity_state(host, token, volt_ent);
        if (!isnan(vv)) out.voltage_v = vv;
    }
    if (cur_ent && cur_ent[0]) {
        float cv = fetch_entity_state(host, token, cur_ent);
        if (!isnan(cv)) out.current_a = cv;
    }
}

float ha_fetch_energy(const char *host, const char *token, const char *entity) {
    return fetch_entity_state(host, token, entity);
}

float ha_fetch_value(const char *host, const char *token, const char *entity) {
    return fetch_entity_state(host, token, entity);
}

void ha_fetch_battery(BatteryData &out, const char *token, const BatteryConfig &bc) {
    if (!bc.host[0] || !token || !token[0]) { out.online = false; return; }
    float power = NAN, soc = NAN;
    if (bc.power_entity[0]) power = fetch_entity_state(bc.host, token, bc.power_entity);
    if (bc.soc_entity[0])   soc   = fetch_entity_state(bc.host, token, bc.soc_entity);
    if (isnan(power) && isnan(soc)) { out.online = false; return; }
    out.online  = true;
    out.power_w = isnan(power) ? 0.0f : power;
    out.soc_pct = isnan(soc)   ? 0.0f : soc;
    if (bc.voltage_entity[0]) {
        float vv = fetch_entity_state(bc.host, token, bc.voltage_entity);
        if (!isnan(vv)) out.voltage_v = vv;
    }
    if (bc.current_entity[0]) {
        float cv = fetch_entity_state(bc.host, token, bc.current_entity);
        if (!isnan(cv)) out.current_a = cv;
    }
    Serial.printf("[ha-bat] power=%.1fW soc=%.0f%% V=%.1f A=%.2f\n",
                  out.power_w, out.soc_pct, out.voltage_v, out.current_a);
}

void ha_fetch_router(RouterData &out, const char *host, const char *token,
                     const RouterConfig &rc) {
    if (!host || !host[0] || !token || !token[0]) { out.online = false; return; }
    float power = NAN;
    if (rc.power_entity[0]) power = fetch_entity_state(host, token, rc.power_entity);
    if (isnan(power)) { out.online = false; return; }
    out.online  = true;
    out.power_w = power;
    out.forced  = false;

    if (rc.energy_entity[0]) {
        float e = fetch_entity_state(host, token, rc.energy_entity);
        if (!isnan(e)) out.today_kwh = e;
    }
    if (rc.active_entity[0]) {
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
    if (rc.voltage_entity[0]) {
        float vv = fetch_entity_state(host, token, rc.voltage_entity);
        if (!isnan(vv)) out.voltage_v = vv;
    }
    if (rc.current_entity[0]) {
        float cv = fetch_entity_state(host, token, rc.current_entity);
        if (!isnan(cv)) out.current_a = cv;
    }
    Serial.printf("[ha-rtr] power=%.1fW active=%d dur=%.2fh V=%.1f A=%.2f\n",
                  out.power_w, out.active, out.duration_h, out.voltage_v, out.current_a);
}

void ha_fetch_solar(SolarData &out, const char *host, const char *token,
                    const char *entity) {
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
