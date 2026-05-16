#include "webserver.h"
#include "device_config.h"
#include "mqtt_pub.h"
#include "sd_logger.h"
#include "types.h"
#include "version.h"
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Update.h>

extern bool ap_active;

static WebServer server(80);

// ─── Buffer temps réel haute fréquence (90 pts × 4 s = ~6 min) ───────────────
struct RTPoint { int32_t ts; float gw; float sw; };
static RTPoint      rt_ring[90];
static int          rt_head  = 0;
static int          rt_count = 0;
static portMUX_TYPE rt_mux   = portMUX_INITIALIZER_UNLOCKED;

// ─── Ring journalier (1 pt / 5 min → 288 pts max pour 24 h) ──────────────────
struct DayPoint { int32_t ts; float gw; float sw; };
static DayPoint      day_ring[288];
static int           day_head    = 0;
static int           day_count   = 0;
static int32_t       day_last_ts = 0;
static int           day_yday    = -1;
static portMUX_TYPE  day_mux     = portMUX_INITIALIZER_UNLOCKED;

void webserver_log(const AppData &d) {
    // Buffer haute fréquence
    RTPoint pt;
    pt.ts = (int32_t)time(nullptr);
    pt.gw = d.grid.power_w;
    pt.sw = d.solar.power_w;
    taskENTER_CRITICAL(&rt_mux);
    rt_ring[rt_head] = pt;
    rt_head = (rt_head + 1) % 90;
    if (rt_count < 90) rt_count++;
    taskEXIT_CRITICAL(&rt_mux);

    // Ring journalier : 1 point toutes les 5 minutes
    int32_t now_ts = pt.ts;
    if (now_ts > 0 && now_ts - day_last_ts >= 300) {
        struct tm ti;
        if (getLocalTime(&ti, 0)) {
            if (ti.tm_yday != day_yday) {
                // Nouveau jour : réinitialiser le ring
                taskENTER_CRITICAL(&day_mux);
                day_head  = 0;
                day_count = 0;
                day_yday  = ti.tm_yday;
                taskEXIT_CRITICAL(&day_mux);
            }
        }
        day_last_ts = now_ts;
        taskENTER_CRITICAL(&day_mux);
        day_ring[day_head] = {now_ts, d.grid.power_w, d.solar.power_w};
        day_head = (day_head + 1) % 288;
        if (day_count < 288) day_count++;
        taskEXIT_CRITICAL(&day_mux);

        // Persistance sur SD : snapshot hors section critique
        static DayPoint ring_snap[288];
        int snap_yday, snap_count, snap_head;
        int32_t snap_last_ts;
        taskENTER_CRITICAL(&day_mux);
        snap_yday    = day_yday;
        snap_count   = day_count;
        snap_head    = day_head;
        snap_last_ts = day_last_ts;
        memcpy(ring_snap, day_ring, sizeof(day_ring));
        taskEXIT_CRITICAL(&day_mux);
        sd_save_day_ring(snap_yday, snap_count, snap_head, snap_last_ts,
                         ring_snap, sizeof(ring_snap));
    }
}

void webserver_restore_day_ring() {
    int saved_yday, saved_count, saved_head;
    int32_t saved_last_ts;
    static DayPoint tmp[288];
    if (!sd_load_day_ring(&saved_yday, &saved_count, &saved_head, &saved_last_ts,
                           tmp, sizeof(tmp))) return;
    struct tm ti;
    if (!getLocalTime(&ti, 0) || ti.tm_yday != saved_yday) return;
    taskENTER_CRITICAL(&day_mux);
    day_yday    = saved_yday;
    day_count   = saved_count;
    day_head    = saved_head;
    day_last_ts = saved_last_ts;
    memcpy(day_ring, tmp, sizeof(day_ring));
    taskEXIT_CRITICAL(&day_mux);
    Serial.printf("[web] ring restaure: %d pts depuis la SD\n", saved_count);
}

// ─── Page HTML (V0.7) ────────────────────────────────────────────────────────
static const char HTML_PAGE[] PROGMEM = R"rawhtml(<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Dash Energy</title>
<style>
:root{--bg:#0d1117;--card:#161b22;--border:#30363d;--text:#e6edf3;--muted:#8b949e;--accent:#58a6ff;--green:#3fb950;--orange:#f4a429;--red:#f85149}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Helvetica,sans-serif;min-height:100vh;font-size:15px}
header{display:flex;align-items:center;justify-content:space-between;padding:12px 20px;border-bottom:1px solid var(--border)}
.logo{font-size:18px;font-weight:700;color:var(--accent)}
.ver{font-size:11px;color:var(--muted);margin-left:8px;vertical-align:middle}
.badges{display:flex;gap:10px;align-items:center}
.badge{background:var(--card);border:1px solid var(--border);padding:3px 10px;border-radius:20px;font-size:12px;color:var(--muted)}
nav{display:flex;border-bottom:1px solid var(--border)}
.tab{flex:1;padding:11px;background:none;border:none;color:var(--muted);cursor:pointer;font-size:14px;border-bottom:2px solid transparent;transition:.15s}
.tab.active{color:var(--text);border-bottom-color:var(--accent)}
.tab:hover:not(.active){color:var(--text)}
#ind-bar{padding:9px 20px;font-size:13px;font-weight:600;text-align:center;transition:background .5s,color .5s}
#ind-bar.ind-green{background:#1a4d2e;color:#3fb950;border-bottom:2px solid #3fb950}
#ind-bar.ind-orange{background:#3d2a00;color:#f4a429;border-bottom:2px solid #f4a429}
#ind-bar.ind-red{background:#3d0f0f;color:#f85149;border-bottom:2px solid #f85149}
#ind-bar.ind-off{background:#1a1e26;color:#8b949e;border-bottom:2px solid #30363d}
main{padding:20px;max-width:900px;margin:0 auto}
.cards{display:grid;grid-template-columns:1fr 1fr;gap:14px;margin-bottom:16px}
@media(max-width:560px){.cards{grid-template-columns:1fr}}
.card{background:var(--card);border:1px solid var(--border);border-radius:10px;padding:18px}
.card-hd{font-size:11px;font-weight:700;letter-spacing:.09em;margin-bottom:10px}
.card-hd.grid{color:var(--accent)}.card-hd.solar{color:var(--orange)}
.status-row{display:flex;align-items:center;gap:7px;font-size:13px;margin-bottom:6px}
.dot{width:8px;height:8px;border-radius:50%;flex-shrink:0;background:var(--muted)}
.dot.ok{background:var(--green)}.dot.err{background:var(--red)}
.big{font-size:34px;font-weight:700;margin:6px 0 2px}
.sub{font-size:13px;color:var(--muted);margin-bottom:2px}
.tag{font-size:11px;color:var(--muted);padding-top:10px;margin-top:10px;border-top:1px solid var(--border)}
.day-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px;margin-bottom:20px}
.si{background:var(--card);border:1px solid var(--border);border-radius:8px;padding:12px}
.si-lbl{font-size:11px;color:var(--muted);margin-bottom:3px}
.si-val{font-size:16px;font-weight:600}
.sys{display:grid;grid-template-columns:repeat(auto-fill,minmax(170px,1fr));gap:10px}
.rssi{display:inline-flex;gap:3px;align-items:flex-end;height:14px;vertical-align:middle}
.rssi span{width:4px;border-radius:1px;background:var(--border)}
.rssi span.on{background:var(--green)}
.gauge-wrap{margin:10px 0 6px}
.gauge-wrap svg{display:block;margin:0 auto}
section{margin-bottom:22px}
h3{font-size:11px;font-weight:700;letter-spacing:.09em;color:var(--muted);margin-bottom:14px;padding-bottom:7px;border-bottom:1px solid var(--border)}
label{display:block;margin-bottom:13px}
label .lbl{display:block;font-size:12px;color:var(--muted);margin-bottom:5px}
input,select{width:100%;padding:8px 11px;background:var(--bg);border:1px solid var(--border);border-radius:6px;color:var(--text);font-size:14px;transition:.15s}
input:focus,select:focus{outline:none;border-color:var(--accent)}
input[type=radio]{width:auto}
.radio-row{display:flex;gap:20px;margin-bottom:10px}
.radio-row label{display:flex;align-items:center;gap:6px;cursor:pointer;margin:0;font-size:14px}
.two{display:grid;grid-template-columns:1fr 1fr;gap:12px}
@media(max-width:480px){.two{grid-template-columns:1fr}}
.row-scan{display:flex;gap:10px;align-items:center;margin-bottom:13px}
.actions{display:flex;gap:10px;flex-wrap:wrap;margin-top:16px}
.btn{padding:9px 18px;border-radius:7px;border:none;cursor:pointer;font-size:14px;font-weight:600;transition:.15s}
.btn-save{background:#1f6feb;color:#fff}.btn-save:hover{background:#388bfd}
.btn-scan{background:var(--card);color:var(--text);border:1px solid var(--border)}.btn-scan:hover{background:var(--border)}
.btn-restart{background:var(--card);color:var(--text);border:1px solid var(--border)}.btn-restart:hover{background:var(--border)}
.info-box{background:var(--card);border:1px solid var(--border);border-radius:7px;padding:10px 14px;font-size:13px;color:var(--muted);margin-bottom:14px}
.ap-banner{background:#1a2d4a;border:1px solid var(--accent);border-radius:7px;padding:10px 14px;font-size:13px;color:var(--accent);margin-bottom:16px;display:none}
.toast{position:fixed;bottom:20px;right:20px;padding:11px 18px;border-radius:8px;font-size:14px;font-weight:600;opacity:0;transition:opacity .3s;pointer-events:none;z-index:99}
.toast.ok{background:var(--green);color:#000;opacity:1}.toast.err{background:var(--red);color:#fff;opacity:1}
</style>
</head>
<body>
<header>
  <div>
    <span class="logo" id="hdr-name">Dash Energy</span>
    <span class="ver" id="hdr-ver"></span>
  </div>
  <div class="badges">
    <span class="badge" id="hdr-ip">--</span>
    <span class="badge" id="hdr-rssi">WiFi --</span>
  </div>
</header>
<div id="ind-bar" class="ind-off">En attente de donnees...</div>
<nav>
  <button class="tab active" onclick="showTab('status',this)">Statut</button>
  <button class="tab" onclick="showTab('config',this)">Configuration</button>
  <button class="tab" onclick="showTab('ota',this)">Mise a jour</button>
</nav>
<main>

<!-- ══ Statut ═══════════════════════════════════════════════════════════════ -->
<div id="pane-status">
  <div class="cards">
    <div class="card">
      <div class="card-hd grid">RESEAU</div>
      <div class="status-row"><span class="dot" id="gd"></span><span id="gs">--</span></div>
      <div class="big" id="gp">--</div>
      <div class="sub" id="gk">--</div>
      <div class="tag" id="gt">--</div>
    </div>
    <div class="card">
      <div class="card-hd solar">SOLAIRE</div>
      <div class="status-row"><span class="dot" id="sd"></span><span id="ss">--</span></div>
      <div class="big" id="sp">--</div>
      <div class="sub" id="sk">--</div>
      <div id="dtu-info" style="display:none">
        <div class="sub" id="sdc">--</div>
        <div class="sub" id="slm">--</div>
      </div>
      <div class="gauge-wrap" id="sg-wrap" style="display:none">
        <svg viewBox="0 0 120 65" width="140" height="76">
          <path d="M10,60 A50,50 0 0,1 110,60" fill="none" stroke="var(--border)" stroke-width="11" stroke-linecap="round"/>
          <path id="sg-arc" d="M10,60 A50,50 0 0,1 110,60" fill="none" stroke="var(--orange)" stroke-width="11" stroke-linecap="round" stroke-dasharray="157" stroke-dashoffset="157" style="transition:stroke-dashoffset .5s,stroke .5s"/>
          <text id="sg-pct" x="60" y="53" text-anchor="middle" fill="var(--muted)" font-size="12" font-family="sans-serif">0%</text>
        </svg>
      </div>
      <div class="tag" id="st">--</div>
    </div>
    <div id="bat-card" class="card" style="display:none">
      <div class="card-hd" style="color:#3fb950">BATTERIE</div>
      <div class="status-row"><span class="dot" id="bd"></span><span id="bs">--</span></div>
      <div class="big" id="bp">--</div>
      <div class="sub" id="bsoc">--</div>
      <div class="tag" id="btt">--</div>
    </div>
  </div>

  <!-- Bilan journalier -->
  <div class="day-grid">
    <div class="si"><div class="si-lbl">Reseau aujourd'hui</div><div class="si-val" id="d-g-kwh">-- kWh</div></div>
    <div class="si"><div class="si-lbl">Solaire aujourd'hui</div><div class="si-val" id="d-s-kwh">-- kWh</div></div>
    <div class="si"><div class="si-lbl">Autoconsommation</div><div class="si-val" id="d-ac">-- %</div></div>
    <div class="si"><div class="si-lbl">Autosuffisance</div><div class="si-val" id="d-as">-- %</div></div>
  </div>

  <!-- Graphique journalier -->
  <section>
    <h3>PUISSANCES DU JOUR (W)</h3>
    <div style="position:relative;height:210px"><canvas id="ch-day"></canvas></div>
    <div id="day-status" style="font-size:12px;color:var(--muted);margin-top:6px;text-align:right"></div>
  </section>
</div>

<!-- ══ Configuration ════════════════════════════════════════════════════════ -->
<div id="pane-config" style="display:none">

<!-- Bandeau point d'accès -->
<div class="ap-banner" id="ap-banner">
  Point d'acces WiFi actif &mdash; <strong id="ap-banner-ip">192.168.4.1</strong> &mdash; SSID : DashEnergy-Config (sans mot de passe)
</div>

<!-- ══ Formulaire WiFi ══════════════════════════════════════════════════════ -->
<form id="wifi-form" onsubmit="saveWifi(event)">
<section>
<h3>CONFIGURATION WIFI</h3>
<div class="info-box" id="wifi-current">WiFi actuel : chargement...</div>
<div class="row-scan">
  <button type="button" class="btn btn-scan" onclick="startScan()">Scanner les reseaux</button>
  <span id="scan-status" style="font-size:13px;color:var(--muted)"></span>
</div>
<label>
  <span class="lbl">Reseau WiFi (SSID)</span>
  <input list="net-list" name="wifi_ssid" placeholder="Nom du reseau" autocomplete="off">
  <datalist id="net-list"></datalist>
</label>
<label>
  <span class="lbl">Mot de passe WiFi</span>
  <input name="wifi_pass" type="password" placeholder="Laisser vide pour ne pas changer">
</label>
<label><span class="lbl">Attribution de l'adresse IP</span></label>
<div class="radio-row">
  <label><input type="radio" name="wifi_dhcp" value="1" onchange="updWifi()"> DHCP (automatique)</label>
  <label><input type="radio" name="wifi_dhcp" value="0" onchange="updWifi()"> IP fixe</label>
</div>
<div id="static-fields" style="display:none">
  <div class="two">
    <label><span class="lbl">Adresse IP</span><input name="static_ip" placeholder="192.168.1.100"></label>
    <label><span class="lbl">Passerelle</span><input name="static_gw" placeholder="192.168.1.1"></label>
  </div>
  <div class="two">
    <label><span class="lbl">Masque de sous-reseau</span><input name="static_nm" placeholder="255.255.255.0"></label>
    <label><span class="lbl">Serveur DNS</span><input name="static_dns" placeholder="8.8.8.8"></label>
  </div>
</div>
<div class="actions">
  <button type="submit" class="btn btn-save">Enregistrer WiFi et redemarrer</button>
</div>
</section>
</form>

<!-- ══ Formulaire appareils ════════════════════════════════════════════════ -->
<form id="cfg" onsubmit="saveCfg(event)">
<section>
<h3>TABLEAU DE BORD</h3>
<label><span class="lbl">Nom de l'appareil</span>
  <input name="device_name" maxlength="31" placeholder="Dash Energy">
</label>
</section>
<section>
<h3>SOURCE RESEAU</h3>
<label><span class="lbl">Type d'appareil</span>
  <select name="grid_device" onchange="updGrid()">
    <option value="0">-- Non configure --</option>
    <optgroup label="Shelly Gen1">
      <option value="1">Shelly EM Gen1 &mdash; 1 sonde</option>
      <option value="2">Shelly EM Gen1 &mdash; 2 sondes</option>
      <option value="3">Shelly 3EM &mdash; 1 phase</option>
      <option value="4">Shelly 3EM &mdash; 2 phases</option>
      <option value="5">Shelly 3EM &mdash; 3 phases</option>
    </optgroup>
    <optgroup label="Shelly Gen2 / Gen3">
      <option value="6">Shelly Pro EM (2 canaux)</option>
      <option value="7">Shelly Pro 3EM (3 canaux)</option>
    </optgroup>
    <optgroup label="Routeur solaire">
      <option value="8">Routeur F1ATB</option>
    </optgroup>
    <optgroup label="Domotique">
      <option value="9">Home Assistant (entite)</option>
    </optgroup>
  </select>
</label>
<div id="grid-ip-wrap">
<label><span class="lbl">Adresse IP / Hote</span>
  <input name="grid_host" placeholder="192.168.1.x">
</label>
</div>
<div id="grid-ha-fields" style="display:none">
  <label><span class="lbl">Entite puissance reseau (entity_id)</span>
    <input name="grid_entity" placeholder="sensor.linky_power" maxlength="63">
  </label>
</div>
</section>
<section>
<h3>SOURCE SOLAIRE</h3>
<label><span class="lbl">Type d'appareil</span>
  <select name="solar_device" onchange="updSolar()">
    <option value="0">-- Non configure --</option>
    <optgroup label="Passerelle DTU (Hoymiles)">
      <option value="1">OpenDTU</option>
      <option value="2">AhoyDTU</option>
    </optgroup>
    <optgroup label="Shelly Gen1">
      <option value="3">Shelly Plug Gen1</option>
      <option value="5">Shelly EM Gen1 &mdash; 1 sonde</option>
      <option value="6">Shelly EM Gen1 &mdash; 2 sondes</option>
      <option value="7">Shelly 3EM &mdash; 1 phase</option>
      <option value="8">Shelly 3EM &mdash; 2 phases</option>
      <option value="9">Shelly 3EM &mdash; 3 phases</option>
    </optgroup>
    <optgroup label="Shelly Gen2 / Gen3">
      <option value="4">Shelly Plug S / Plus 1PM Gen2/3</option>
    </optgroup>
    <optgroup label="Onduleur direct">
      <option value="11">Fronius Symo / Primo / Galvo</option>
    </optgroup>
    <optgroup label="Domotique">
      <option value="10">Home Assistant (entite)</option>
    </optgroup>
  </select>
</label>
<label><span class="lbl">Adresse IP / Hote</span>
  <input name="solar_host" placeholder="192.168.1.x">
</label>
<div id="solar-ha-fields" style="display:none">
  <label><span class="lbl">Entite puissance solaire (entity_id)</span>
    <input name="solar_entity" placeholder="sensor.solaredge_power" maxlength="63">
  </label>
</div>
<div id="dtu-fields" style="display:none">
  <div id="dtu-auth" class="two">
    <label><span class="lbl">Utilisateur OpenDTU</span><input name="solar_user" placeholder="admin"></label>
    <label><span class="lbl">Mot de passe OpenDTU</span><input name="solar_pass" type="password"></label>
  </div>
  <label><span class="lbl" id="dtu-serial-lbl">Numero de serie onduleur</span>
    <input name="solar_serial" id="dtu-serial-inp" placeholder="114181234567" maxlength="63">
  </label>
</div>
</section>
<label><span class="lbl">Puissance crete solaire (Wc) &mdash; active la jauge ecran</span>
  <input name="solar_max_w" type="number" min="0" max="99999" placeholder="0 = jauge desactivee">
</label>
</section>
<section>
<h3>BATTERIE (OPTIONNEL)</h3>
<label><span class="lbl">Type de batterie</span>
  <select name="battery_device" onchange="updBattery()">
    <option value="0">-- Non configure --</option>
    <optgroup label="Passerelle ESPHome">
      <option value="1">JK-BMS via syssi/esphome-jk-bms</option>
    </optgroup>
  </select>
</label>
<div id="bat-ip-wrap" style="display:none">
<label><span class="lbl">Adresse IP du bridge ESPHome</span>
  <input name="battery_host" placeholder="192.168.1.x">
</label>
<div class="info-box">
  Necessite un ESP32 bridge avec ESPHome + <a href="https://github.com/syssi/esphome-jk-bms" target="_blank" rel="noopener" style="color:var(--accent)">syssi/esphome-jk-bms</a>.<br>
  L'ESP bridge se connecte au BMS en Bluetooth et expose les donnees en HTTP sur le reseau local.
</div>
</div>
</section>
<div id="ha-section" style="display:none">
<section>
<h3>HOME ASSISTANT</h3>
<div class="info-box">
  Token Long-Lived Access Token genere dans HA : Profil &rarr; Securite &rarr; Jetons d'acces longue duree.<br>
  L'adresse IP/hote est celle configuree pour chaque source (ex: 192.168.1.10:8123).
</div>
<label><span class="lbl">Jeton d'acces HA (Bearer Token)</span>
  <input name="ha_token" type="password" maxlength="191" placeholder="eyJhbGc...">
</label>
</section>
</div>
<section>
<h3>AFFICHAGE</h3>
<label><span class="lbl">Fuseau horaire</span>
  <select name="timezone">
    <option value="CET-1CEST,M3.5.0,M10.5.0/3">France / Belgique / Espagne (CET/CEST)</option>
    <option value="GMT0BST,M3.5.0/1,M10.5.0">Royaume-Uni (GMT/BST)</option>
    <option value="WET0WEST,M3.5.0/1,M10.5.0">Portugal (WET/WEST)</option>
    <option value="EET-2EEST,M3.5.0/3,M10.5.0/4">Europe de l'Est (EET/EEST)</option>
    <option value="UTC0">UTC</option>
    <option value="EST5EDT,M3.2.0,M11.1.0">USA Est (EST/EDT)</option>
    <option value="CST6CDT,M3.2.0,M11.1.0">USA Centre (CST/CDT)</option>
    <option value="PST8PDT,M3.2.0,M11.1.0">USA Ouest (PST/PDT)</option>
  </select>
</label>
<label><span class="lbl">Orientation ecran</span>
  <select name="display_rotation">
    <option value="0">Normal (0deg)</option>
    <option value="2">Retourne (180deg)</option>
  </select>
</label>
<div class="actions">
  <button type="submit" class="btn btn-save">Enregistrer et redemarrer</button>
  <button type="button" class="btn btn-restart" onclick="restart()">Redemarrer</button>
</div>
</form>

<!-- ══ Formulaire MQTT ══════════════════════════════════════════════════════ -->
<form id="mqtt-form" onsubmit="saveMqtt(event)">
<section>
<h3>MQTT &amp; HOME ASSISTANT</h3>
<div class="info-box">
  Les donnees sont publiees sur le broker MQTT apres chaque collecte.<br>
  Activez <strong>Auto-decouverte HA</strong> pour que les capteurs apparaissent automatiquement dans Home Assistant.
</div>
<div class="radio-row" style="margin-bottom:14px">
  <label><input type="checkbox" name="mqtt_enabled" value="1"> Activer MQTT</label>
  <label><input type="checkbox" name="mqtt_ha"> Auto-decouverte Home Assistant</label>
</div>
<div class="two">
  <label><span class="lbl">Adresse du broker</span><input name="mqtt_host" placeholder="192.168.1.x"></label>
  <label><span class="lbl">Port</span><input name="mqtt_port" type="number" min="1" max="65535" placeholder="1883"></label>
</div>
<div class="two">
  <label><span class="lbl">Utilisateur (optionnel)</span><input name="mqtt_user" placeholder="homeassistant"></label>
  <label><span class="lbl">Mot de passe</span><input name="mqtt_pass" type="password"></label>
</div>
<label><span class="lbl">Topic de base</span>
  <input name="mqtt_topic" placeholder="dashenergy" maxlength="31">
</label>
<div class="actions">
  <button type="submit" class="btn btn-save">Enregistrer MQTT et redemarrer</button>
</div>
</section>
</form>

<!-- ══ Systeme ════════════════════════════════════════════════════════════════ -->
<section id="sys-section">
<h3>SYSTEME</h3>
<div class="sys">
  <div class="si"><div class="si-lbl">Adresse IP</div><div class="si-val" id="i-ip">--</div></div>
  <div class="si"><div class="si-lbl">Reseau WiFi</div><div class="si-val" id="i-ssid">--</div></div>
  <div class="si"><div class="si-lbl">Signal WiFi</div><div class="si-val" id="i-rssi">--</div></div>
  <div class="si"><div class="si-lbl">Uptime</div><div class="si-val" id="i-up">--</div></div>
  <div class="si"><div class="si-lbl">Memoire utilisee</div><div class="si-val" id="i-heap">--</div></div>
  <div class="si"><div class="si-lbl">PSRAM utilisee</div><div class="si-val" id="i-psram">--</div></div>
  <div class="si"><div class="si-lbl">CPU</div><div class="si-val" id="i-cpu">--</div></div>
  <div class="si"><div class="si-lbl">Firmware</div><div class="si-val" id="i-build">--</div></div>
  <div class="si"><div class="si-lbl">Heure NTP</div><div class="si-val" id="i-time">--</div></div>
  <div class="si" id="si-ap" style="display:none"><div class="si-lbl">Point d'acces</div><div class="si-val" id="i-ap">--</div></div>
</div>
</section>

</div><!-- pane-config -->

<!-- ══ OTA ══════════════════════════════════════════════════════════════════ -->
<div id="pane-ota" style="display:none">
<section>
<h3>MISE A JOUR FIRMWARE (OTA)</h3>
<div class="info-box">
  Selectionnez le fichier <strong>firmware.bin</strong> genere par PlatformIO<br>
  (<code>.pio/build/jc8048w550/firmware.bin</code>).<br>
  L'ESP redemarrera automatiquement apres la mise a jour.
</div>
<form id="ota-form" onsubmit="doOta(event)">
  <label>
    <span class="lbl">Fichier firmware (.bin)</span>
    <input type="file" name="firmware" accept=".bin" required
           style="border:none;padding:4px 0;background:transparent">
  </label>
  <div id="ota-progress" style="display:none;margin:14px 0">
    <div style="background:var(--border);border-radius:4px;height:8px">
      <div id="ota-bar" style="background:var(--accent);height:8px;border-radius:4px;width:0;transition:width .2s"></div>
    </div>
    <div id="ota-pct" style="font-size:13px;color:var(--muted);margin-top:6px;text-align:center">0%</div>
  </div>
  <div class="actions">
    <button type="submit" class="btn btn-save" id="ota-btn">Flasher le firmware</button>
  </div>
</form>
</section>
</div>

</main>
<div class="toast" id="toast"></div>
<script>
var tid=null, dayTid=null, chDay=null;

function showTab(name,btn){
  document.querySelectorAll('.tab').forEach(function(b){b.classList.remove('active');});
  btn.classList.add('active');
  ['status','config','ota'].forEach(function(p){
    document.getElementById('pane-'+p).style.display=(p===name?'':'none');
  });
  if(name==='status'){startRefresh();}
  else{
    clearInterval(tid);tid=null;
    clearInterval(dayTid);dayTid=null;
    if(name==='config'){loadCfg();fetchStatus();if(!tid)tid=setInterval(fetchStatus,3000);}
  }
}

function updSolar(){
  var v=parseInt(document.querySelector('[name=solar_device]').value);
  var isDtu=(v===1||v===2);
  var isHa=(v===10);
  document.getElementById('dtu-fields').style.display=isDtu?'':'none';
  document.getElementById('solar-ha-fields').style.display=isHa?'':'none';
  if(isDtu){
    var isAhoy=(v===2);
    document.getElementById('dtu-auth').style.display=isAhoy?'none':'';
    document.getElementById('dtu-serial-lbl').textContent=isAhoy?'ID onduleur (0, 1, 2...)':'Numero de serie onduleur';
    document.getElementById('dtu-serial-inp').placeholder=isAhoy?'0':'114181234567';
  }
  updHaSection();
}
function updGrid(){
  var v=parseInt(document.querySelector('[name=grid_device]').value);
  var isHa=(v===9);
  document.getElementById('grid-ha-fields').style.display=isHa?'':'none';
  updHaSection();
}
function updHaSection(){
  var gv=parseInt(document.querySelector('[name=grid_device]').value);
  var sv=parseInt(document.querySelector('[name=solar_device]').value);
  var needHa=(gv===9||sv===10);
  document.getElementById('ha-section').style.display=needHa?'':'none';
}
function updBattery(){
  var v=parseInt(document.querySelector('[name=battery_device]').value);
  document.getElementById('bat-ip-wrap').style.display=v>0?'':'none';
}
function updWifi(){
  var v=document.querySelector('[name=wifi_dhcp]:checked');
  document.getElementById('static-fields').style.display=(v&&v.value==='0')?'':'none';
}

/* ── Scan WiFi ── */
async function startScan(){
  document.getElementById('scan-status').textContent='Scan en cours...';
  document.getElementById('net-list').innerHTML='';
  try{
    await fetch('/api/wifi/scan',{method:'POST'});
    setTimeout(pollNetworks,3500);
  }catch(e){document.getElementById('scan-status').textContent='Erreur';}
}
async function pollNetworks(){
  try{
    var d=await fetch('/api/wifi/networks').then(function(r){return r.json();});
    if(d.scanning){setTimeout(pollNetworks,2000);return;}
    var dl=document.getElementById('net-list');
    dl.innerHTML='';
    (d.networks||[]).forEach(function(n){
      var o=document.createElement('option');
      o.value=n.ssid;
      dl.appendChild(o);
    });
    document.getElementById('scan-status').textContent=(d.networks?d.networks.length:0)+' reseau(x) trouve(s)';
  }catch(e){}
}

/* ── Utilitaires ── */
function bars(rssi){
  var pct=Math.max(0,Math.min(100,2*(rssi+100)));
  var n=Math.round(pct/25);
  var h='<span class="rssi">';
  for(var i=1;i<=4;i++) h+='<span style="height:'+(i*3+3)+'px" class="'+(i<=n?'on':'')+'"></span>';
  return h+'</span>';
}
function fmtW(w){
  if(Math.abs(w)>=1000) return (w/1000).toFixed(2)+' kW';
  return Math.round(w)+' W';
}
function fmtUp(s){
  var h=Math.floor(s/3600),m=Math.floor((s%3600)/60),sc=s%60;
  return h+'h '+pad(m)+'m '+pad(sc)+'s';
}
function fmtMem(used,total){
  if(!total) return (used/1024).toFixed(0)+' KB';
  var pct=Math.round(used/total*100);
  return (used/1024).toFixed(0)+' / '+(total/1024).toFixed(0)+' KB ('+pct+'%)';
}
function pad(n){return n<10?'0'+n:n;}
function setDot(id,ok){var d=document.getElementById(id);d.className='dot '+(ok===null?'':ok?'ok':'err');}

function setGauge(arcId,pctId,val,max,colLo,colHi){
  var arc=document.getElementById(arcId);
  var ptx=document.getElementById(pctId);
  if(!arc) return;
  var pct=max>0?Math.max(0,Math.min(1,Math.abs(val)/max)):0;
  var offset=(157*(1-pct)).toFixed(1);
  arc.style.strokeDashoffset=offset;
  if(max>0){
    arc.style.stroke=pct>=0.8?colHi:(pct>=0.4?'var(--orange)':colLo);
    if(ptx){ptx.textContent=Math.round(pct*100)+'%';ptx.style.fill='var(--text)';}
  } else {
    arc.style.strokeDashoffset='157';
    if(ptx){ptx.textContent='';ptx.style.fill='var(--muted)';}
  }
}

/* ── Indicateur vert/rouge/neutre ── */
function updateIndicator(gp,sp){
  var bar=document.getElementById('ind-bar');
  if(gp<-10){
    bar.className='ind-bar ind-green';
    bar.textContent=(sp>0?'Surplus solaire — ':'Energie — ')+fmtW(-gp)+' exportes vers le reseau';
  } else if(gp>10){
    bar.className='ind-bar ind-red';
    bar.textContent=(sp>0?'Solaire insuffisant — ':'')+'Import reseau : '+fmtW(gp);
  } else {
    bar.className='ind-bar ind-off';
    bar.textContent=sp>0?'Equilibre — Production = Consommation locale':'En attente de donnees...';
  }
}

/* ── Status ── */
function fetchStatus(){
  fetch('/api/status').then(function(r){return r.json();}).then(function(d){
    document.getElementById('hdr-name').textContent=d.device_name||'Dash Energy';
    document.getElementById('hdr-ver').textContent=d.version||'';
    document.title=(d.device_name||'Dash Energy')+' — Statut';
    document.getElementById('hdr-ip').textContent=d.ip||'--';
    document.getElementById('hdr-rssi').innerHTML='WiFi '+bars(d.rssi||0);

    setDot('gd',d.grid.online);
    document.getElementById('gs').textContent=d.grid.online?'En ligne':'Hors ligne';
    document.getElementById('gp').textContent=d.grid.online?fmtW(d.grid.power_w):'--';
    var gkwh=d.grid.today_kwh>0?d.grid.today_kwh.toFixed(2)+' kWh auj.':'--';
    document.getElementById('gk').textContent=d.grid.online?gkwh:'--';
    document.getElementById('gt').textContent=(d.grid.device||'Non configure')+(d.grid.host?' — '+d.grid.host:'');

    setDot('sd',d.solar.online);
    document.getElementById('ss').textContent=d.solar.online?'En ligne':'Hors ligne';
    document.getElementById('sp').textContent=d.solar.online?fmtW(d.solar.power_w):'--';
    var skwh=d.solar.today_kwh>0?d.solar.today_kwh.toFixed(2)+' kWh auj.':'--';
    document.getElementById('sk').textContent=d.solar.online?skwh:'--';
    document.getElementById('st').textContent=(d.solar.device||'Non configure')+(d.solar.host?' — '+d.solar.host:'');
    var isDtu=d.solar.is_opendtu;
    document.getElementById('dtu-info').style.display=isDtu?'':'none';
    if(isDtu){
      document.getElementById('sdc').textContent='DC : '+d.solar.dc_voltage.toFixed(1)+' V';
      document.getElementById('slm').textContent='Limite : '+d.solar.limit_pct+' %';
    }

    // kWh journaliers dans les tuiles
    document.getElementById('d-g-kwh').textContent=d.grid.today_kwh>0?d.grid.today_kwh.toFixed(2)+' kWh':'--';
    document.getElementById('d-s-kwh').textContent=d.solar.today_kwh>0?d.solar.today_kwh.toFixed(2)+' kWh':'--';

    document.getElementById('i-ip').textContent=d.ip||'--';
    document.getElementById('i-ssid').textContent=d.ssid||'--';
    document.getElementById('i-rssi').innerHTML=bars(d.rssi||0)+' '+d.rssi+' dBm';
    document.getElementById('i-up').textContent=fmtUp(d.uptime_s||0);
    document.getElementById('i-heap').textContent=fmtMem(d.total_heap-d.free_heap,d.total_heap);
    document.getElementById('i-psram').textContent=fmtMem(d.psram_total-d.psram_free,d.psram_total);
    document.getElementById('i-cpu').textContent=(d.cpu_mhz||0)+' MHz';
    document.getElementById('i-build').textContent=(d.version||'')+(d.build?' — '+d.build:'');
    document.getElementById('i-time').textContent=d.time?(d.date+' '+d.time):'NTP non sync';
    var apEl=document.getElementById('si-ap');
    var apBanner=document.getElementById('ap-banner');
    if(d.ap_active){
      apEl.style.display='';
      document.getElementById('i-ap').textContent='DashEnergy-Config — '+d.ap_ip;
      apBanner.style.display='';
      document.getElementById('ap-banner-ip').textContent=d.ap_ip;
    } else {
      apEl.style.display='none';
      apBanner.style.display='none';
    }

    var solMax=d.solar&&d.solar.max_w>0?d.solar.max_w:0;
    if(solMax>0){
      document.getElementById('sg-wrap').style.display='';
      setGauge('sg-arc','sg-pct',d.solar.power_w,solMax,'var(--muted)','var(--green)');
    } else {
      document.getElementById('sg-wrap').style.display='none';
    }

    var batCard=document.getElementById('bat-card');
    if(d.battery&&d.battery.configured){
      batCard.style.display='';
      setDot('bd',d.battery.online);
      document.getElementById('bs').textContent=d.battery.online?'En ligne':'Hors ligne';
      if(d.battery.online){
        var bpStr=d.battery.power_w>=0?'+'+fmtW(d.battery.power_w)+' (decharge)':fmtW(d.battery.power_w)+' (charge)';
        document.getElementById('bp').textContent=bpStr;
        document.getElementById('bsoc').textContent='SOC : '+d.battery.soc_pct.toFixed(0)+' %';
      }else{document.getElementById('bp').textContent='--';document.getElementById('bsoc').textContent='--';}
      document.getElementById('btt').textContent=d.battery.device||'';
    }else{batCard.style.display='none';}
    updateIndicator(d.grid.power_w, d.solar.power_w);
  }).catch(function(){});
}
function startRefresh(){
  fetchStatus();
  fetchDaily();
  if(!tid) tid=setInterval(fetchStatus,3000);
  if(!dayTid) dayTid=setInterval(fetchDaily,300000); // toutes les 5 min
}

/* ── Graphique journalier ── */
function todayMidnight(){
  var d=new Date(); d.setHours(0,0,0,0); return d.getTime()/1000;
}
function fmtTs(ts){
  var d=new Date(ts*1000);
  return pad(d.getHours())+':'+pad(d.getMinutes());
}

function fetchDaily(){
  if(typeof Chart==='undefined'){
    var s=document.createElement('script');
    s.src='https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js';
    s.onload=function(){fetchDaily();};
    s.onerror=function(){document.getElementById('day-status').textContent='Chart.js non disponible (connexion requise)';};
    document.head.appendChild(s);
    return;
  }
  fetch('/api/daily').then(function(r){return r.json();}).then(function(data){
    var midnight=todayMidnight();
    var pts=(data.pts||[]).filter(function(p){return p.ts>=midnight;});

    // Autoconsommation et autosuffisance journalieres
    document.getElementById('d-ac').textContent=data.ac!==undefined?(data.ac+'%'):'-- %';
    document.getElementById('d-as').textContent=data.as_!==undefined?(data.as_+'%'):'-- %';

    var st=document.getElementById('day-status');
    if(!pts.length){st.textContent='Pas encore de donnees (debut de journee)';return;}

    var labels=pts.map(function(p){return fmtTs(p.ts);});
    var gp=pts.map(function(p){return p.gp;});
    var sp=pts.map(function(p){return p.sp;});
    var opts={
      animation:false,
      plugins:{legend:{labels:{color:'#e6edf3'}}},
      scales:{
        x:{ticks:{color:'#8b949e',maxTicksLimit:12},grid:{color:'#30363d'}},
        y:{ticks:{color:'#8b949e'},grid:{color:'#30363d'},min:0}
      }
    };
    if(chDay) chDay.destroy();
    chDay=new Chart(document.getElementById('ch-day'),{type:'line',data:{labels:labels,datasets:[
      {label:'Consommation reseau (W)',data:gp,borderColor:'#58a6ff',backgroundColor:'rgba(88,166,255,.12)',fill:true,tension:.3,pointRadius:0,borderWidth:2},
      {label:'Production solaire (W)',data:sp,borderColor:'#f4a429',backgroundColor:'rgba(244,164,41,.12)',fill:true,tension:.3,pointRadius:0,borderWidth:2}
    ]},options:opts});
    st.textContent=pts.length+' points — '+fmtTs(pts[0].ts)+' → '+fmtTs(pts[pts.length-1].ts);
  }).catch(function(){});
}

/* ── Config ── */
function loadCfg(){
  fetch('/api/config').then(function(r){return r.json();}).then(function(c){
    var f=document.getElementById('cfg');
    f.elements.device_name.value=c.device_name||'';
    f.elements.grid_device.value=c.grid_device||0;
    f.elements.grid_host.value=c.grid_host||'';
    f.elements.solar_device.value=c.solar_device||0;
    f.elements.solar_host.value=c.solar_host||'';
    f.elements.solar_user.value=c.solar_user||'';
    f.elements.solar_pass.value=c.solar_pass||'';
    f.elements.solar_serial.value=c.solar_serial||'';
    f.elements.solar_max_w.value=c.solar_max_w||0;
    f.elements.display_rotation.value=c.display_rotation||0;
    f.elements.grid_entity.value=c.grid_entity||'';
    f.elements.solar_entity.value=c.solar_entity||'';
    f.elements.ha_token.value=c.ha_token||'';
    f.elements.timezone.value=c.timezone||'CET-1CEST,M3.5.0,M10.5.0/3';
    updSolar();
    updGrid();
    f.elements.battery_device.value=c.battery_device||0;
    f.elements.battery_host.value=c.battery_host||'';
    updBattery();

    var mf=document.getElementById('mqtt-form');
    mf.elements.mqtt_enabled.checked=(c.mqtt_enabled==1);
    mf.elements.mqtt_ha.checked=(c.mqtt_ha!==false);
    mf.elements.mqtt_host.value=c.mqtt_host||'';
    mf.elements.mqtt_port.value=c.mqtt_port||1883;
    mf.elements.mqtt_user.value=c.mqtt_user||'';
    mf.elements.mqtt_pass.value=c.mqtt_pass||'';
    mf.elements.mqtt_topic.value=c.mqtt_topic||'dashenergy';

    var wf=document.getElementById('wifi-form');
    wf.elements.wifi_ssid.value=c.wifi_ssid||'';
    var dhcp=(c.wifi_dhcp===undefined||c.wifi_dhcp)?1:0;
    document.querySelectorAll('[name=wifi_dhcp]').forEach(function(r){r.checked=(parseInt(r.value)===dhcp);});
    wf.elements.static_ip.value=c.static_ip||'';
    wf.elements.static_gw.value=c.static_gw||'';
    wf.elements.static_nm.value=c.static_nm||'255.255.255.0';
    wf.elements.static_dns.value=c.static_dns||'8.8.8.8';
    updWifi();

    document.getElementById('wifi-current').textContent='WiFi actuel : '+(c.current_ssid||'--')+' | IP : '+(c.current_ip||'--')+' | '+c.rssi_dbm+' dBm';
  }).catch(function(){toast('Erreur chargement','err');});
}

function saveCfg(e){
  e.preventDefault();
  var fd=new FormData(e.target),data={};
  fd.forEach(function(v,k){data[k]=v;});
  data.grid_device=parseInt(data.grid_device)||0;
  data.solar_device=parseInt(data.solar_device)||0;
  data.solar_max_w=parseInt(data.solar_max_w)||0;
  data.display_rotation=parseInt(data.display_rotation)||0;
  data.battery_device=parseInt(data.battery_device)||0;
  data.ha_token=data.ha_token||'';
  data.grid_entity=data.grid_entity||'';
  data.solar_entity=data.solar_entity||'';
  data.timezone=data.timezone||'CET-1CEST,M3.5.0,M10.5.0/3';
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)})
    .then(function(r){
      if(r.ok){toast('Sauvegarde OK — redemarrage...','ok');setTimeout(function(){location.reload();},4500);}
      else toast('Erreur sauvegarde','err');
    }).catch(function(){toast('Erreur reseau','err');});
}

function saveWifi(e){
  e.preventDefault();
  var fd=new FormData(e.target),data={};
  fd.forEach(function(v,k){data[k]=v;});
  var dhcp=document.querySelector('[name=wifi_dhcp]:checked');
  data.wifi_dhcp=dhcp?parseInt(dhcp.value):1;
  fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)})
    .then(function(r){
      if(r.ok){toast('WiFi sauvegarde — redemarrage...','ok');setTimeout(function(){location.reload();},4500);}
      else toast('Erreur sauvegarde WiFi','err');
    }).catch(function(){toast('Erreur reseau','err');});
}

function restart(){
  if(!confirm('Redemarrer le Dash Energy ?')) return;
  fetch('/api/restart',{method:'POST'}).then(function(){
    toast('Redemarrage en cours...','ok');
    setTimeout(function(){location.reload();},4500);
  }).catch(function(){});
}
function toast(msg,type){
  var t=document.getElementById('toast');
  t.textContent=msg;t.className='toast '+type;
  setTimeout(function(){t.className='toast';},3500);
}

/* ── MQTT ── */
function saveMqtt(e){
  e.preventDefault();
  var fd=new FormData(e.target),data={};
  fd.forEach(function(v,k){data[k]=v;});
  data.mqtt_enabled=e.target.elements.mqtt_enabled.checked?1:0;
  data.mqtt_ha=e.target.elements.mqtt_ha.checked?1:0;
  data.mqtt_port=parseInt(data.mqtt_port)||1883;
  fetch('/api/mqtt',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)})
    .then(function(r){
      if(r.ok){toast('MQTT sauvegarde — redemarrage...','ok');setTimeout(function(){location.reload();},4500);}
      else toast('Erreur MQTT','err');
    }).catch(function(){toast('Erreur reseau','err');});
}

/* ── OTA ── */
function doOta(e){
  e.preventDefault();
  var file=e.target.elements.firmware.files[0];
  if(!file){toast('Selectionnez un fichier .bin','err');return;}
  if(!confirm('Mettre a jour avec '+file.name+' ('+Math.round(file.size/1024)+' KB) ?')) return;
  var xhr=new XMLHttpRequest();
  xhr.open('POST','/update');
  xhr.upload.onprogress=function(ev){
    if(!ev.lengthComputable) return;
    var pct=Math.round(ev.loaded/ev.total*100);
    document.getElementById('ota-bar').style.width=pct+'%';
    document.getElementById('ota-pct').textContent=pct+'%';
  };
  xhr.onload=function(){
    if(xhr.status===200&&xhr.responseText==='OK'){
      toast('Mise a jour OK — redemarrage...','ok');
      document.getElementById('ota-btn').disabled=true;
    } else {
      toast('Erreur OTA: '+(xhr.responseText||xhr.status),'err');
      document.getElementById('ota-btn').disabled=false;
    }
  };
  xhr.onerror=function(){toast('Erreur reseau','err');};
  document.getElementById('ota-progress').style.display='';
  document.getElementById('ota-btn').disabled=true;
  var fd=new FormData();
  fd.append('firmware',file);
  xhr.send(fd);
}

startRefresh();
</script>
</body>
</html>)rawhtml";

// ─── Handlers ─────────────────────────────────────────────────────────────────
static void handle_root() {
    server.send_P(200, "text/html; charset=utf-8", HTML_PAGE);
}

static void handle_status() {
    GridData    grid    = {};
    SolarData   solar   = {};
    BatteryData battery = {};

    if (g_data.mutex && xSemaphoreTake(g_data.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        grid    = g_data.grid;
        solar   = g_data.solar;
        battery = g_data.battery;
        xSemaphoreGive(g_data.mutex);
    }

    JsonDocument doc;
    doc["version"]     = FIRMWARE_VERSION;
    doc["build"]       = __DATE__ " " __TIME__;
    doc["device_name"] = g_cfg.device_name;
    doc["ip"]          = WiFi.localIP().toString();
    doc["ssid"]        = WiFi.SSID();
    doc["rssi"]        = WiFi.RSSI();
    doc["uptime_s"]    = millis() / 1000UL;
    doc["free_heap"]   = ESP.getFreeHeap();
    doc["total_heap"]  = ESP.getHeapSize();
    doc["psram_free"]  = ESP.getFreePsram();
    doc["psram_total"] = ESP.getPsramSize();
    doc["cpu_mhz"]     = ESP.getCpuFreqMHz();
    doc["ap_active"]   = ap_active;
    doc["ap_ip"]       = ap_active ? "192.168.4.1" : "";

    struct tm ti;
    if (getLocalTime(&ti, 0)) {
        char tbuf[10], dbuf[12];
        strftime(tbuf, sizeof(tbuf), "%H:%M:%S", &ti);
        strftime(dbuf, sizeof(dbuf), "%d/%m/%Y",  &ti);
        doc["time"] = tbuf;
        doc["date"] = dbuf;
    } else {
        doc["time"] = "";
        doc["date"] = "";
    }

    auto g = doc["grid"].to<JsonObject>();
    g["online"]    = grid.online;
    g["power_w"]   = grid.power_w;
    g["today_kwh"] = grid.today_kwh;
    g["device"]    = grid_device_label(g_cfg.grid_device);
    g["host"]      = g_cfg.grid_host;

    auto s = doc["solar"].to<JsonObject>();
    s["online"]     = solar.online;
    s["power_w"]    = solar.power_w;
    s["today_kwh"]  = solar.today_kwh;
    s["dc_voltage"] = solar.dc_voltage;
    s["limit_pct"]  = solar.limit_pct;
    s["device"]     = solar_device_label(g_cfg.solar_device);
    s["host"]       = g_cfg.solar_host;
    s["is_opendtu"] = (g_cfg.solar_device == SolarDevice::OPENDTU);
    s["max_w"]      = g_cfg.solar_max_w;

    auto b = doc["battery"].to<JsonObject>();
    b["configured"] = (g_cfg.battery_device != BatteryDevice::NONE);
    b["online"]     = battery.online;
    b["power_w"]    = battery.power_w;
    b["soc_pct"]    = battery.soc_pct;
    b["device"]     = battery_device_label(g_cfg.battery_device);

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

// ─── Données journalières ─────────────────────────────────────────────────────
static void handle_daily() {
    static DayPoint snap[288];
    int head, count;
    taskENTER_CRITICAL(&day_mux);
    memcpy(snap, day_ring, day_count * sizeof(DayPoint));
    head  = day_head;
    count = day_count;
    taskEXIT_CRITICAL(&day_mux);

    // Calcul autoconsommation / autosuffisance depuis les données instantanées
    double g_kwh = 0, s_kwh = 0, self_kwh = 0;
    const double dt_h = 300.0 / 3600.0;  // 5 min par point
    int start = (count < 288) ? 0 : head;
    for (int i = 0; i < count; i++) {
        int idx = (start + i) % 288;
        float gp = snap[idx].gw;
        float sp = snap[idx].sw;
        if (gp > 0) g_kwh  += gp * dt_h / 1000.0;
        if (sp > 0) s_kwh  += sp * dt_h / 1000.0;
        float self = sp + (gp < 0 ? gp : 0.0f);
        if (self > 0) self_kwh += self * dt_h / 1000.0;
    }
    int ac_pct  = (s_kwh  > 0.01) ? (int)min(100.0, self_kwh / s_kwh        * 100.0) : 0;
    int as_pct  = (g_kwh + s_kwh > 0.01) ? (int)min(100.0, self_kwh / (g_kwh + s_kwh) * 100.0) : 0;

    // Sérialisation (max 300 pts decimés)
    static char buf[14000];
    size_t pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "{\"pts\":[");
    int step = (count > 300) ? count / 300 : 1;
    bool first = true;
    for (int i = 0; i < count; i += step) {
        int idx = (start + i) % 288;
        if (!first) buf[pos++] = ',';
        first = false;
        int n = snprintf(buf + pos, sizeof(buf) - pos,
                         "{\"ts\":%ld,\"gp\":%.0f,\"sp\":%.0f}",
                         (long)snap[idx].ts, snap[idx].gw, snap[idx].sw);
        pos += n;
        if (pos > sizeof(buf) - 200) break;
    }
    snprintf(buf + pos, sizeof(buf) - pos,
             "],\"g_kwh\":%.2f,\"s_kwh\":%.2f,\"ac\":%d,\"as_\":%d}",
             g_kwh, s_kwh, ac_pct, as_pct);
    server.send(200, "application/json", buf);
}

static void handle_config_get() {
    JsonDocument doc;
    doc["device_name"]  = g_cfg.device_name;
    doc["grid_device"]  = (int)g_cfg.grid_device;
    doc["grid_host"]    = g_cfg.grid_host;
    doc["solar_device"] = (int)g_cfg.solar_device;
    doc["solar_host"]   = g_cfg.solar_host;
    doc["solar_user"]   = g_cfg.solar_user;
    doc["solar_pass"]   = g_cfg.solar_pass;
    doc["solar_serial"] = g_cfg.solar_serial;
    doc["solar_max_w"]       = g_cfg.solar_max_w;
    doc["display_rotation"]  = g_cfg.display_rotation;
    doc["ha_token"]          = g_cfg.ha_token;
    doc["grid_entity"]       = g_cfg.grid_entity;
    doc["solar_entity"]      = g_cfg.solar_entity;
    doc["timezone"]          = g_cfg.timezone;
    doc["battery_device"]    = (int)g_cfg.battery_device;
    doc["battery_host"]      = g_cfg.battery_host;

    doc["mqtt_enabled"] = g_mqtt.enabled ? 1 : 0;
    doc["mqtt_host"]    = g_mqtt.host;
    doc["mqtt_port"]    = g_mqtt.port;
    doc["mqtt_user"]    = g_mqtt.user;
    doc["mqtt_pass"]    = g_mqtt.pass;
    doc["mqtt_topic"]   = g_mqtt.base_topic;
    doc["mqtt_ha"]      = g_mqtt.ha_discovery ? 1 : 0;

    {
        Preferences p;
        p.begin("wifi_cfg", true);
        doc["wifi_ssid"] = p.getString("ssid", "");
        p.end();
        p.begin("wifi_net", true);
        doc["wifi_dhcp"]  = p.getBool("dhcp", true) ? 1 : 0;
        doc["static_ip"]  = p.getString("static_ip", "");
        doc["static_gw"]  = p.getString("static_gw", "");
        doc["static_nm"]  = p.getString("static_nm",  "255.255.255.0");
        doc["static_dns"] = p.getString("static_dns",  "8.8.8.8");
        p.end();
    }
    doc["current_ssid"] = WiFi.SSID();
    doc["current_ip"]   = WiFi.localIP().toString();
    doc["rssi_dbm"]     = String(WiFi.RSSI()) + " dBm";

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void handle_config_post() {
    String body = server.arg("plain");
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        server.send(400, "application/json", "{\"error\":\"json invalide\"}");
        return;
    }

    DeviceConfig cfg = g_cfg;
    strncpy(cfg.device_name, doc["device_name"] | cfg.device_name, sizeof(cfg.device_name) - 1);
    cfg.grid_device = (GridDevice)(int)(doc["grid_device"] | (int)cfg.grid_device);
    strncpy(cfg.grid_host,    doc["grid_host"]    | cfg.grid_host,    sizeof(cfg.grid_host)    - 1);
    cfg.solar_device = (SolarDevice)(int)(doc["solar_device"] | (int)cfg.solar_device);
    strncpy(cfg.solar_host,   doc["solar_host"]   | cfg.solar_host,   sizeof(cfg.solar_host)   - 1);
    strncpy(cfg.solar_user,   doc["solar_user"]   | cfg.solar_user,   sizeof(cfg.solar_user)   - 1);
    strncpy(cfg.solar_pass,   doc["solar_pass"]   | cfg.solar_pass,   sizeof(cfg.solar_pass)   - 1);
    strncpy(cfg.solar_serial, doc["solar_serial"] | cfg.solar_serial, sizeof(cfg.solar_serial) - 1);
    cfg.solar_max_w       = (uint16_t)(int)(doc["solar_max_w"]      | (int)cfg.solar_max_w);
    cfg.display_rotation  = (uint8_t)(int)(doc["display_rotation"] | (int)cfg.display_rotation);
    strncpy(cfg.ha_token,     doc["ha_token"]     | cfg.ha_token,     sizeof(cfg.ha_token)     - 1);
    strncpy(cfg.grid_entity,  doc["grid_entity"]  | cfg.grid_entity,  sizeof(cfg.grid_entity)  - 1);
    strncpy(cfg.solar_entity, doc["solar_entity"] | cfg.solar_entity, sizeof(cfg.solar_entity) - 1);
    strncpy(cfg.timezone,     doc["timezone"]     | cfg.timezone,     sizeof(cfg.timezone)     - 1);
    cfg.battery_device = (BatteryDevice)(int)(doc["battery_device"] | (int)cfg.battery_device);
    strncpy(cfg.battery_host, doc["battery_host"] | cfg.battery_host, sizeof(cfg.battery_host) - 1);

    device_config_save(cfg);
    g_cfg = cfg;

    server.send(200, "application/json", "{\"ok\":true}");
    delay(300);
    ESP.restart();
}

static void handle_wifi_config() {
    String body = server.arg("plain");
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        server.send(400, "application/json", "{\"error\":\"json invalide\"}");
        return;
    }

    const char *ssid = doc["wifi_ssid"] | "";
    const char *pass = doc["wifi_pass"] | "";
    if (strlen(ssid) > 0) {
        Preferences p;
        p.begin("wifi_cfg", false);
        p.putString("ssid", ssid);
        if (strlen(pass) > 0) p.putString("pass", pass);
        p.end();
    }

    {
        Preferences p;
        p.begin("wifi_net", false);
        int dhcp = doc["wifi_dhcp"] | 1;
        p.putBool("dhcp", dhcp != 0);
        p.putString("static_ip",  doc["static_ip"]  | "");
        p.putString("static_gw",  doc["static_gw"]  | "");
        p.putString("static_nm",  doc["static_nm"]  | "255.255.255.0");
        p.putString("static_dns", doc["static_dns"] | "8.8.8.8");
        p.end();
    }

    server.send(200, "application/json", "{\"ok\":true}");
    delay(300);
    ESP.restart();
}

static void handle_wifi_scan_start() {
    WiFi.scanDelete();
    WiFi.scanNetworks(true);
    server.send(200, "application/json", "{\"scanning\":true}");
}

static void handle_wifi_networks() {
    int n = WiFi.scanComplete();
    JsonDocument doc;
    if (n == WIFI_SCAN_RUNNING || n == WIFI_SCAN_FAILED) {
        doc["scanning"] = true;
    } else {
        doc["scanning"] = false;
        auto arr = doc["networks"].to<JsonArray>();
        for (int i = 0; i < n && i < 24; i++) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue;
            auto net = arr.add<JsonObject>();
            net["ssid"]      = ssid;
            net["rssi"]      = WiFi.RSSI(i);
            net["encrypted"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        }
    }
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void handle_mqtt_post() {
    String body = server.arg("plain");
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        server.send(400, "application/json", "{\"error\":\"json invalide\"}");
        return;
    }
    MQTTConfig cfg = g_mqtt;
    cfg.enabled     = (int)(doc["mqtt_enabled"] | 0) != 0;
    cfg.ha_discovery= (int)(doc["mqtt_ha"]      | 1) != 0;
    cfg.port        = (uint16_t)(int)(doc["mqtt_port"] | 1883);
    strncpy(cfg.host,       doc["mqtt_host"]  | cfg.host,       sizeof(cfg.host)-1);
    strncpy(cfg.user,       doc["mqtt_user"]  | cfg.user,       sizeof(cfg.user)-1);
    strncpy(cfg.pass,       doc["mqtt_pass"]  | cfg.pass,       sizeof(cfg.pass)-1);
    strncpy(cfg.base_topic, doc["mqtt_topic"] | cfg.base_topic, sizeof(cfg.base_topic)-1);
    mqtt_config_save(cfg);
    g_mqtt = cfg;
    server.send(200, "application/json", "{\"ok\":true}");
    delay(300);
    ESP.restart();
}

static void handle_history() {
    if (!sd_ready()) {
        server.send(503, "application/json", "{\"error\":\"SD non disponible\"}");
        return;
    }
    int n = 200;
    if (server.hasArg("n")) n = server.arg("n").toInt();
    if (n < 1) n = 1;
    if (n > 500) n = 500;
    static char hist_buf[22000];
    if (sd_get_history(hist_buf, sizeof(hist_buf), n))
        server.send(200, "application/json", hist_buf);
    else
        server.send(500, "application/json", "{\"error\":\"buffer insuffisant\"}");
}

static void handle_realtime() {
    RTPoint snap[90];
    int head, count;
    taskENTER_CRITICAL(&rt_mux);
    memcpy(snap, rt_ring, sizeof(rt_ring));
    head  = rt_head;
    count = rt_count;
    taskEXIT_CRITICAL(&rt_mux);

    if (count == 0) {
        server.send(200, "application/json", "[]");
        return;
    }

    static char buf[8192];
    size_t pos = 0;
    buf[pos++] = '[';
    int start = (count < 90) ? 0 : head;
    for (int i = 0; i < count; i++) {
        int idx = (start + i) % 90;
        char entry[80];
        int n = snprintf(entry, sizeof(entry),
                         "{\"ts\":%ld,\"gp\":%.1f,\"sp\":%.1f}",
                         (long)snap[idx].ts, snap[idx].gw, snap[idx].sw);
        if (pos + n + 2 >= sizeof(buf)) break;
        memcpy(buf + pos, entry, n);
        pos += n;
        if (i < count - 1) buf[pos++] = ',';
    }
    buf[pos++] = ']';
    buf[pos]   = '\0';
    server.send(200, "application/json", buf);
}

static void handle_restart() {
    server.send(200, "application/json", "{\"ok\":true}");
    delay(300);
    ESP.restart();
}

// Portail captif : toute URL inconnue redirige vers la page principale
static void handle_404() {
    if (ap_active) {
        server.sendHeader("Location", "http://192.168.4.1/");
        server.send(302, "text/plain", "Redirect");
    } else {
        server.send(404, "text/plain", "Not found");
    }
}

// ─── Tâche FreeRTOS (core 0) ──────────────────────────────────────────────────
static void web_task(void *) {
    server.on("/",                  HTTP_GET,  handle_root);
    server.on("/api/status",        HTTP_GET,  handle_status);
    server.on("/api/daily",         HTTP_GET,  handle_daily);
    server.on("/api/config",        HTTP_GET,  handle_config_get);
    server.on("/api/config",        HTTP_POST, handle_config_post);
    server.on("/api/wifi",          HTTP_POST, handle_wifi_config);
    server.on("/api/wifi/scan",     HTTP_POST, handle_wifi_scan_start);
    server.on("/api/wifi/networks", HTTP_GET,  handle_wifi_networks);
    server.on("/api/restart",       HTTP_POST, handle_restart);
    server.on("/api/realtime",      HTTP_GET,  handle_realtime);
    server.on("/api/mqtt",          HTTP_POST, handle_mqtt_post);
    server.on("/api/history",       HTTP_GET,  handle_history);
    server.onNotFound(handle_404);

    // ── OTA ──────────────────────────────────────────────────────────────────
    server.on("/update", HTTP_POST,
        []() {
            server.sendHeader("Connection", "close");
            server.send(200, "text/plain", Update.hasError() ? "ERREUR" : "OK");
            delay(500);
            ESP.restart();
        },
        []() {
            HTTPUpload &up = server.upload();
            if (up.status == UPLOAD_FILE_START) {
                Serial.printf("[ota] Debut: %s\n", up.filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN))
                    Serial.println("[ota] begin() ECHEC");
            } else if (up.status == UPLOAD_FILE_WRITE) {
                if (Update.write(up.buf, up.currentSize) != up.currentSize)
                    Serial.println("[ota] write() ECHEC");
            } else if (up.status == UPLOAD_FILE_END) {
                if (Update.end(true))
                    Serial.printf("[ota] OK: %u octets\n", up.totalSize);
                else
                    Serial.println("[ota] end() ECHEC");
            }
        }
    );

    server.begin(80);

    MDNS.begin("dashenergy");
    MDNS.addService("http", "tcp", 80);

    Serial.printf("[web] " FIRMWARE_VERSION " — http://%s/  http://dashenergy.local/\n",
                  WiFi.localIP().toString().c_str());
    if (ap_active) Serial.println("[web] AP actif aussi — http://192.168.4.1/");

    for (;;) {
        server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void webserver_start() {
    xTaskCreatePinnedToCore(web_task, "web", 12288, nullptr, 1, nullptr, 0);
}
