// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Alexandre Richard -- https://github.com/Alex77138/Energy-Dashboard
#include "opendtu.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

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

// ── OpenDTU (un seul numéro de série) ─────────────────────────────────────────
static bool opendtu_fetch_single(SolarData &out,
                                  const char *host, const char *user,
                                  const char *pass, const char *serial) {
    JsonDocument doc;
    if (http_get_json(host, user, pass, "/api/livedata/status", doc) != 200) {
        out.online = false;
        return false;
    }

    // Cherche l'onduleur correspondant au numéro de série dans le tableau
    JsonVariant target;
    JsonArray inverters = doc["inverters"].as<JsonArray>();
    for (JsonVariant inv : inverters) {
        String sn = inv["serial"].as<String>();
        if (serial[0] == '\0' || sn == serial) {
            target = inv;
            break;
        }
    }
    if (target.isNull()) target = doc["inverters"][0];
    if (target.isNull()) { out.online = false; return false; }

    out.power_w   = target["AC"]["0"]["Power"]["v"].as<float>();
    out.today_kwh = target["AC"]["0"]["YieldDay"]["v"].as<float>() / 1000.0f;
    out.dc_voltage= target["DC"]["0"]["Voltage"]["v"].as<float>();
    out.online    = target["reachable"].as<bool>();

    JsonDocument lim;
    if (serial[0] && http_get_json(host, user, pass, "/api/limit/status", lim) == 200) {
        out.limit_pct = lim[serial]["limit_relative"].as<int>();
    }
    return true;
}

// ── OpenDTU multi-serial "SN1/SN2/SN3" ───────────────────────────────────────
bool opendtu_fetch(SolarData &out,
                   const char *host, const char *user,
                   const char *pass, const char *serials_str) {
    out = {};
    char buf[64];
    strncpy(buf, serials_str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    bool any = false;
    char *tok = strtok(buf, "/");
    if (!tok) return opendtu_fetch_single(out, host, user, pass, "");

    while (tok) {
        while (*tok == ' ') ++tok;
        SolarData tmp = {};
        if (opendtu_fetch_single(tmp, host, user, pass, tok)) {
            out.power_w   += tmp.power_w;
            out.today_kwh += tmp.today_kwh;
            out.dc_voltage = tmp.dc_voltage;
            out.limit_pct  = tmp.limit_pct;
            if (tmp.online) any = true;
        }
        tok = strtok(nullptr, "/");
    }
    out.online = any;
    return any;
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
