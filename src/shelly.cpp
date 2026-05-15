#include "shelly.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

static bool http_get(const char *url, JsonDocument &doc) {
    HTTPClient http;
    http.setTimeout(3000);
    if (!http.begin(url)) return false;
    int code = http.GET();
    if (code != 200) { http.end(); return false; }
    DeserializationError err = deserializeJson(doc, http.getString());
    http.end();
    return !err;
}

// ─── Shelly EM Gen1 — /status → emeters[n].power / .total (Wh) ──────────────
bool shelly_em_fetch(MeasureData &out, const char *host, int probes) {
    JsonDocument doc;
    char url[80];
    snprintf(url, sizeof(url), "http://%s/status", host);
    if (!http_get(url, doc)) { out.online = false; return false; }

    float power = 0, energy = 0;
    for (int i = 0; i < probes && i < 2; i++) {
        power  += doc["emeters"][i]["power"].as<float>();
        energy += doc["emeters"][i]["total"].as<float>();
    }
    out.power_w   = power;
    out.today_kwh = energy / 1000.0f;
    out.online    = true;
    return true;
}

// ─── Shelly 3EM Gen1 — /status → emeters[0..2] ───────────────────────────────
bool shelly_3em_fetch(MeasureData &out, const char *host, int probes) {
    JsonDocument doc;
    char url[80];
    snprintf(url, sizeof(url), "http://%s/status", host);
    if (!http_get(url, doc)) { out.online = false; return false; }

    float power = 0, energy = 0;
    for (int i = 0; i < probes && i < 3; i++) {
        power  += doc["emeters"][i]["power"].as<float>();
        energy += doc["emeters"][i]["total"].as<float>();
    }
    out.power_w   = power;
    out.today_kwh = energy / 1000.0f;
    out.online    = true;
    return true;
}

// ─── Shelly Pro EM / Pro 3EM Gen2/3 — /rpc/EM.GetStatus?id=0 ────────────────
bool shelly_pro_em_fetch(MeasureData &out, const char *host, int channels) {
    JsonDocument doc;
    char url[96];
    snprintf(url, sizeof(url), "http://%s/rpc/EM.GetStatus?id=0", host);
    if (!http_get(url, doc)) { out.online = false; return false; }

    out.power_w   = doc["a_act_power"].as<float>() + doc["b_act_power"].as<float>();
    out.today_kwh = (doc["a_total_act_energy"].as<float>() +
                     doc["b_total_act_energy"].as<float>()) / 1000.0f;
    if (channels >= 3) {
        out.power_w   += doc["c_act_power"].as<float>();
        out.today_kwh += doc["c_total_act_energy"].as<float>() / 1000.0f;
    }
    out.online = true;
    return true;
}

// ─── Shelly Plug Gen1 — /status → meters[0].power / .total (Wmin) ────────────
bool shelly_plug_g1_fetch(MeasureData &out, const char *host) {
    JsonDocument doc;
    char url[80];
    snprintf(url, sizeof(url), "http://%s/status", host);
    if (!http_get(url, doc)) { out.online = false; return false; }

    out.power_w   = doc["meters"][0]["power"].as<float>();
    out.today_kwh = doc["meters"][0]["total"].as<float>() / 60000.0f; // Wmin → kWh
    out.online    = true;
    return true;
}

// ─── Shelly Plug Gen2/3 — /rpc/Switch.GetStatus?id=0 ─────────────────────────
bool shelly_plug_g2g3_fetch(MeasureData &out, const char *host) {
    JsonDocument doc;
    char url[80];
    snprintf(url, sizeof(url), "http://%s/rpc/Switch.GetStatus?id=0", host);
    if (!http_get(url, doc)) { out.online = false; return false; }

    out.power_w   = doc["apower"].as<float>();
    out.today_kwh = doc["aenergy"]["total"].as<float>() / 1000.0f;
    out.online    = true;
    return true;
}
