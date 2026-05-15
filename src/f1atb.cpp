#include "f1atb.h"
#include <HTTPClient.h>

// Séparateurs ASCII du protocole F1ATB
static const char GS = '\x1d'; // Group Separator  — sépare les groupes
static const char RS = '\x1e'; // Record Separator — sépare les valeurs d'un groupe

// ─── Utilitaires de parsing ───────────────────────────────────────────────────

// Retourne la valeur flottante du champ `field` dans le groupe `group`
// Structure /ajax_data :
//   G0 : en-tête (date, source, tarif...)
//   G1 : Maison  [0]=PwSoutiré [1]=PwInjecté [2]=VAS [3]=VAI [4]=EJourS [5]=EJourI [6]=EtotS [7]=EtotI
//   G2 : Triac   même structure — puissance et énergie routées vers le chauffe-eau
//   G3+ : actuateurs, sources supplémentaires
static float get_field(const String &body, int group, int field) {
    int g = 0, g_start = 0;

    // Avance jusqu'au bon groupe
    for (int i = 0; i < (int)body.length(); i++) {
        if ((uint8_t)body[i] == (uint8_t)GS) {
            if (g == group) {
                // on était dans le bon groupe — extraire le champ
                break;
            }
            g++;
            g_start = i + 1;
        }
    }
    if (g < group) return NAN; // groupe absent

    // Trouver la fin du groupe
    int g_end = body.indexOf(GS, g_start);
    if (g_end < 0) g_end = body.length();
    String grp = body.substring(g_start, g_end);

    // Extraire le champ `field` (séparé par RS)
    int pos = 0, cur = 0;
    for (int i = 0; i <= (int)grp.length(); i++) {
        if (i == (int)grp.length() || (uint8_t)grp[i] == (uint8_t)RS) {
            if (cur == field) {
                String val = grp.substring(pos, i);
                val.trim();
                return val.toFloat();
            }
            pos = i + 1;
            cur++;
        }
    }
    return NAN;
}

static bool is_valid(float v) { return !isnan(v) && v > -100.0f; }

// ─── Lecture /ajax_data ───────────────────────────────────────────────────────
bool f1atb_fetch(const char *host, RouterData &out, ShellyEMData *grid_out) {
    char url[64];
    snprintf(url, sizeof(url), "http://%s/ajax_data", host);

    HTTPClient http;
    http.setTimeout(3000);
    if (!http.begin(url)) { out.online = false; return false; }
    int code = http.GET();
    if (code != 200) { http.end(); out.online = false; return false; }

    String body = http.getString();
    http.end();

    // ── G2 : triac (présence = G2[0] valide, ouverture = G2[2]) ─────────────
    float pws_t = get_field(body, 2, 0); // détection présence (0 si pas de Shelly EM)
    float vas   = get_field(body, 2, 2); // VAS = % ouverture triac (0–100)
    float ej_t  = get_field(body, 2, 4); // énergie jour soutirée (Wh)

    // Debug : affiche tous les champs G2 pour identifier le bon champ
    Serial.printf("[F1ATB] G2: [0]=%.1f [1]=%.1f [2]=%.1f [3]=%.1f [4]=%.1f\n",
        get_field(body, 2, 0), get_field(body, 2, 1),
        get_field(body, 2, 2), get_field(body, 2, 3), get_field(body, 2, 4));

    if (!is_valid(pws_t)) { out.online = false; return false; }

    out.triac_pct = is_valid(vas) ? vas : 0.0f;
    out.today_kwh = is_valid(ej_t) ? ej_t / 1000.0f : 0.0f;
    out.online    = true;

    // État forcé : G3[0] = valeur Force (1=forcé ON, 0=auto, -1=retour auto)
    // À vérifier avec un vrai appareil — l'index peut différer selon la version F1ATB
    float force_val = get_field(body, 3, 0);
    out.forced = is_valid(force_val) && (int)force_val == 1;

    // ── G1 : données réseau (si le Shelly EM du routeur est en ligne) ─────────
    if (grid_out) {
        float pws_m = get_field(body, 1, 0);
        float pwi_m = get_field(body, 1, 1);
        float ej_m  = get_field(body, 1, 4);
        if (is_valid(pws_m) && is_valid(pwi_m)) {
            grid_out->power_w   = pws_m - pwi_m;
            grid_out->today_kwh = is_valid(ej_m) ? ej_m / 1000.0f : 0.0f;
            grid_out->online    = true;
        }
    }

    return true;
}

// ─── Commande marche forcée ───────────────────────────────────────────────────
bool f1atb_set_forced(const char *host, const char *url_on, const char *url_off, bool on) {
    char url[96];
    snprintf(url, sizeof(url), "http://%s%s", host, on ? url_on : url_off);
    HTTPClient http;
    http.setTimeout(3000);
    if (!http.begin(url)) return false;
    int code = http.GET();
    http.end();
    return (code == 200);
}
