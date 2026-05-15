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
