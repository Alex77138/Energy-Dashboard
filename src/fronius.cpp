#include "fronius.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

void fronius_fetch(SolarData &out, const char *host) {
    if (!host[0]) { out.online = false; return; }

    char url[128];
    snprintf(url, sizeof(url),
             "http://%s/solar_api/v1/GetPowerFlowRealtimeData.fcgi", host);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(4000);
    int code = http.GET();
    if (code != 200) {
        Serial.printf("[fronius] HTTP %d\n", code);
        http.end();
        out.online = false;
        return;
    }

    String body = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, body)) { out.online = false; return; }

    // Body.Data.Site.P_PV peut être null quand l'onduleur est éteint
    JsonVariant pv = doc["Body"]["Data"]["Site"]["P_PV"];
    if (pv.isNull() || !pv.is<float>()) {
        out.online  = true;   // onduleur joignable mais pas de production
        out.power_w = 0;
    } else {
        float p = pv.as<float>();
        out.online  = true;
        out.power_w = p > 0 ? p : 0;
    }

    JsonVariant ed = doc["Body"]["Data"]["Site"]["E_Day"];
    out.today_kwh = ed.isNull() ? 0.0f : ed.as<float>() / 1000.0f;

    out.dc_voltage = 0;
    out.limit_pct  = 100;

    Serial.printf("[fronius] P_PV=%.1fW E_Day=%.3fkWh\n", out.power_w, out.today_kwh);
}
