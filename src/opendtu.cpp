// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Alexandre Richard -- https://github.com/Alex77138/Energy-Dashboard
#include "opendtu.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

// Allocateur PSRAM pour ArduinoJson : le JSON livedata OpenDTU peut dépasser
// 12 KB pour 3 onduleurs HMS-1000-2T (radio_stats + AC + DC + INV) et sature
// le heap interne de l'ESP32-S3.  La PSRAM (8 MB) absorbe sans problème.
struct PsramAllocator {
    void* allocate(size_t n)              { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
    void  deallocate(void* p)             { heap_caps_free(p); }
    void* reallocate(void* p, size_t n)   { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
};
// 32 KB en PSRAM — largement suffisant pour le livedata de 3 onduleurs (~12 KB)
using PsramJsonDoc = ArduinoJson::BasicJsonDocument<PsramAllocator>;

static int http_get_json(const char *host, const char *user, const char *pass,
                         const char *path, JsonDocument &doc) {
    char url[128];
    snprintf(url, sizeof(url), "http://%s%s", host, path);
    HTTPClient http;
    http.setTimeout(4000);
    http.begin(url);
    if (user && user[0]) http.setAuthorization(user, pass);
    int code = http.GET();
    if (code == 200) deserializeJson(doc, http.getString());
    http.end();
    return code;
}

static int http_post(const char *host, const char *user, const char *pass,
                     const char *path, const char *body) {
    char url[128];
    snprintf(url, sizeof(url), "http://%s%s", host, path);
    HTTPClient http;
    http.setTimeout(4000);
    http.begin(url);
    if (user && user[0]) http.setAuthorization(user, pass);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int code = http.POST((uint8_t *)body, strlen(body));
    http.end();
    return code;
}

// ── OpenDTU — un seul appel HTTP, gère SN1/SN2/SN3 ──────────────────────────
// Stratégie :
//  1. Essaie les champs "ac"/"dc" (minuscules) per-inverter (API récente OpenDTU)
//  2. Fallback sur doc["total"] si les champs per-inverter sont absents
//     (certaines versions n'incluent pas ac/dc par onduleur)
bool opendtu_fetch(SolarData &out,
                   const char *host, const char *user,
                   const char *pass, const char *serials_str) {
    out = {};

    PsramJsonDoc doc(32768);
    char url[128];
    snprintf(url, sizeof(url), "http://%s/api/livedata/status", host);
    HTTPClient http;
    http.setTimeout(4000);
    http.begin(url);
    if (user && user[0]) http.setAuthorization(user, pass);
    int code = http.GET();
    if (code == 200) deserializeJson(doc, http.getStream());
    http.end();
    if (code != 200) { out.online = false; return false; }

    // Construit la liste des serials recherchés (séparés par '/')
    char buf[64];
    strncpy(buf, serials_str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    // Parcourt les onduleurs du JSON, accumule ceux qui correspondent
    bool any_reachable  = false;
    bool per_inv_found  = false;
    JsonArray inverters = doc["inverters"].as<JsonArray>();

    char *tok = strtok(buf, "/");
    while (tok) {
        while (*tok == ' ') ++tok;
        for (JsonVariant inv : inverters) {
            if (tok[0] && inv["serial"].as<String>() != tok) continue;
            if (inv["reachable"].as<bool>()) any_reachable = true;

            // API récente OpenDTU : clés "ac" et "dc" en minuscules
            float pwr = inv["ac"]["0"]["Power"]["v"].as<float>();
            float kwh = inv["ac"]["0"]["YieldDay"]["v"].as<float>() / 1000.0f;
            float vdc = inv["dc"]["0"]["Voltage"]["v"].as<float>();

            if (!inv["ac"]["0"]["Power"]["v"].isNull()) {
                out.power_w   += pwr;
                out.today_kwh += kwh;
                if (vdc > 0) out.dc_voltage = vdc;
                per_inv_found = true;
            }
            break;
        }
        tok = strtok(nullptr, "/");
    }

    // Fallback : doc["total"] si aucun champ per-inverter n'a été trouvé.
    // On proratise : (nb serials configurés) / (nb total onduleurs sur le DTU)
    // → 1 source × 3 serials = 3/3 × total  ✓
    // → 3 sources × 1 serial = 1/3 × total  ✓  (évite le triple-comptage)
    if (!per_inv_found && !doc["total"]["Power"]["v"].isNull()) {
        int configured = 0;
        { char tmp[64]; strncpy(tmp, serials_str, sizeof(tmp)-1);
          char *t = strtok(tmp, "/");
          while (t) { configured++; t = strtok(nullptr, "/"); } }
        int total_inv = (int)inverters.size();
        float ratio = (total_inv > 0) ? (float)configured / total_inv : 1.0f;
        out.power_w   = doc["total"]["Power"]["v"].as<float>()   * ratio;
        out.today_kwh = doc["total"]["YieldDay"]["v"].as<float>() / 1000.0f * ratio;
    }

    out.online = any_reachable;

    // Limit % depuis le premier serial
    {
        char buf2[64];
        strncpy(buf2, serials_str, sizeof(buf2) - 1);
        buf2[sizeof(buf2) - 1] = '\0';
        char *first = strtok(buf2, "/");
        if (first && first[0]) {
            while (*first == ' ') ++first;
            JsonDocument lim;
            if (http_get_json(host, user, pass, "/api/limit/status", lim) == 200)
                out.limit_pct = lim[first]["limit_relative"].as<int>();
        }
    }

    return out.online || out.power_w > 0;
}

// ── AhoyDTU (un seul ID) ──────────────────────────────────────────────────────
static bool ahoydtu_fetch_single(SolarData &out, const char *host, const char *id_str) {
    int inv_id = id_str[0] ? atoi(id_str) : 0;
    char path[32];
    snprintf(path, sizeof(path), "/api/inverter/id/%d", inv_id);

    char url[128];
    snprintf(url, sizeof(url), "http://%s%s", host, path);
    HTTPClient http;
    http.setTimeout(4000);
    http.begin(url);
    int code = http.GET();
    if (code != 200) { http.end(); out.online = false; return false; }

    JsonDocument doc;
    deserializeJson(doc, http.getString());
    http.end();

    float p_ac      = doc["ch0"][0].as<float>();
    float yield_day = doc["ch0"][7].as<float>();
    bool  enabled   = doc["enabled"].as<bool>();

    // Tension DC depuis le premier canal si disponible
    float u_dc = 0;
    JsonVariant ch0 = doc["ch"][0];
    if (!ch0.isNull()) u_dc = ch0["U_DC"].as<float>();

    out.online     = enabled && (p_ac > 0.0f || yield_day > 0.0f);
    out.power_w    = p_ac;
    out.today_kwh  = yield_day / 1000.0f;
    out.dc_voltage = u_dc;
    out.limit_pct  = 100;
    return true;
}

// ── AhoyDTU multi-ID "ID1/ID2/ID3" ───────────────────────────────────────────
bool ahoydtu_fetch(SolarData &out, const char *host, const char *ids_str) {
    out = {};
    char buf[64];
    strncpy(buf, ids_str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    bool any = false;
    char *tok = strtok(buf, "/");
    if (!tok) return ahoydtu_fetch_single(out, host, "0");

    while (tok) {
        while (*tok == ' ') ++tok;
        SolarData tmp = {};
        if (ahoydtu_fetch_single(tmp, host, tok)) {
            out.power_w   += tmp.power_w;
            out.today_kwh += tmp.today_kwh;
            out.dc_voltage = tmp.dc_voltage;
            if (tmp.online) any = true;
        }
        tok = strtok(nullptr, "/");
    }
    out.online = any;
    return any;
}

bool opendtu_set_power(const char *host, const char *user,
                       const char *pass, const char *serial, bool enabled) {
    char body[80];
    snprintf(body, sizeof(body), "serial=%s&power=%s",
             serial, enabled ? "true" : "false");
    return http_post(host, user, pass, "/api/power/config", body) == 200;
}

bool opendtu_set_limit(const char *host, const char *user,
                       const char *pass, const char *serial, int percent) {
    percent = constrain(percent, 2, 100);
    char body[80];
    snprintf(body, sizeof(body), "serial=%s&limit_type=1&limit_value=%d",
             serial, percent);
    return http_post(host, user, pass, "/api/limit/config", body) == 200;
}
