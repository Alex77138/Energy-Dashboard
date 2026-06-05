// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Alexandre Richard -- https://github.com/Alex77138/Energy-Dashboard
#include "webserver.h"
#include "device_config.h"
#include "mqtt_pub.h"
#include "sd_logger.h"
#include "types.h"
#include "version.h"
#include "ui.h"
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
static RTPoint      *rt_ring  = nullptr;  // PSRAM — alloué dans webserver_start()
static int           rt_head  = 0;
static int           rt_count = 0;
static portMUX_TYPE  rt_mux   = portMUX_INITIALIZER_UNLOCKED;

// ─── Ring journalier (1 pt / 5 min → 288 pts max pour 24 h) ──────────────────
struct DayPoint { int32_t ts; float gw; float sw; };
static DayPoint     *day_ring   = nullptr;  // PSRAM — alloué dans webserver_start()
static int           day_head    = 0;
static int           day_count   = 0;
static int32_t       day_last_ts = 0;
static int           day_yday    = -1;

// ─── Buffers PSRAM partagés (non concurrent — même tâche web) ────────────────
static DayPoint *s_log_snap = nullptr;  // snapshot SD dans webserver_log (poll task)
static DayPoint *s_rst_tmp  = nullptr;  // buffer restore ring (poll task, 1 fois)
static DayPoint *s_day_snap = nullptr;  // snapshot handle_daily (web task)
static char     *s_web_buf  = nullptr;  // 22 KB JSON commun aux handlers web
static portMUX_TYPE  day_mux     = portMUX_INITIALIZER_UNLOCKED;

void webserver_log(const AppData &d) {
    if (!rt_ring || !day_ring) return;
    // Buffer haute fréquence
    RTPoint pt;
    pt.ts = (int32_t)time(nullptr);
    pt.gw = d.grid.power_w;
    pt.sw = fabsf(d.solar.power_w);
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
        day_ring[day_head] = {now_ts, d.grid.power_w, fabsf(d.solar.power_w)};
        day_head = (day_head + 1) % 288;
        if (day_count < 288) day_count++;
        taskEXIT_CRITICAL(&day_mux);

        // Persistance sur SD : snapshot hors section critique
        if (s_log_snap) {
            int snap_yday, snap_count, snap_head;
            int32_t snap_last_ts;
            taskENTER_CRITICAL(&day_mux);
            snap_yday    = day_yday;
            snap_count   = day_count;
            snap_head    = day_head;
            snap_last_ts = day_last_ts;
            memcpy(s_log_snap, day_ring, sizeof(DayPoint) * 288);
            taskEXIT_CRITICAL(&day_mux);
            sd_save_day_ring(snap_yday, snap_count, snap_head, snap_last_ts,
                             s_log_snap, sizeof(DayPoint) * 288);
        }
    }
}

void webserver_restore_day_ring() {
    if (!s_rst_tmp) return;
    int saved_yday, saved_count, saved_head;
    int32_t saved_last_ts;
    if (!sd_load_day_ring(&saved_yday, &saved_count, &saved_head, &saved_last_ts,
                           s_rst_tmp, sizeof(DayPoint) * 288)) return;
    struct tm ti;
    if (!getLocalTime(&ti, 0) || ti.tm_yday != saved_yday) return;
    taskENTER_CRITICAL(&day_mux);
    day_yday    = saved_yday;
    day_count   = saved_count;
    day_head    = saved_head;
    day_last_ts = saved_last_ts;
    memcpy(day_ring, s_rst_tmp, sizeof(DayPoint) * 288);
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
.multi-item{border:1px solid var(--border);border-radius:8px;padding:12px;margin-bottom:10px}
.multi-hd{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px;font-size:13px;font-weight:700;color:var(--text)}
.btn-del{padding:3px 10px;border-radius:5px;border:1px solid var(--red);background:none;color:var(--red);cursor:pointer;font-size:12px}
details{border:1px solid var(--border);border-radius:8px;margin-bottom:8px;overflow:hidden}
details[open]{border-color:var(--accent)}
summary{padding:11px 16px;cursor:pointer;font-size:11px;font-weight:700;letter-spacing:.09em;color:var(--muted);display:flex;align-items:center;gap:7px;user-select:none;list-style:none}
summary::-webkit-details-marker{display:none}
summary::before{content:'▸';font-size:11px;transition:transform .2s;flex-shrink:0}
details[open]>summary::before{transform:rotate(90deg)}
details[open]>summary{color:var(--text)}
.acc-body{padding:0 16px 16px}
.host-row{display:grid;grid-template-columns:1fr auto;gap:8px;align-items:flex-end;margin-bottom:13px}
.host-row label{margin:0}
.host-row select{height:36px;padding:0 8px;font-size:12px;color:var(--muted);white-space:nowrap}
.sec-badge{font-size:10px;padding:1px 7px;border-radius:10px;background:var(--border);color:var(--muted);margin-left:auto}
.sum-body{flex:1;min-width:0}
.sum-desc{display:block;font-size:11px;font-weight:400;letter-spacing:.01em;color:var(--muted);margin-top:3px;text-transform:none}
</style>
</head>
<body>
<header>
  <div>
    <span class="logo" id="hdr-name">Dash Energy</span>
    <span class="ver" id="hdr-ver"></span>
  </div>
  <div class="badges">
    <span class="badge" id="demo-badge" style="display:none;background:#f4a429;color:#000;font-weight:700;border-color:#f4a429">DEMO</span>
    <span class="badge" id="hdr-ip">--</span>
    <span class="badge" id="hdr-rssi">WiFi --</span>
  </div>
</header>
<div id="ind-bar" class="ind-off">En attente de donnees...</div>
<div id="scr-nav" style="display:flex;gap:6px;padding:8px 20px;border-bottom:1px solid var(--border);background:var(--card);overflow-x:auto;flex-wrap:wrap">
  <button class="btn btn-scan" onclick="navTo(0)" style="font-size:12px;padding:5px 14px">Principal</button>
  <button class="btn btn-scan" onclick="navTo(1)" style="font-size:12px;padding:5px 14px">Solaire</button>
  <button class="btn btn-scan" onclick="navTo(2)" style="font-size:12px;padding:5px 14px">Reseau</button>
  <button class="btn btn-scan" onclick="navTo(3)" style="font-size:12px;padding:5px 14px">Batterie</button>
  <button class="btn btn-scan" onclick="navTo(4)" style="font-size:12px;padding:5px 14px">Routeur</button>
</div>
<nav>
  <button class="tab active" onclick="showTab('status',this)">Statut</button>
  <button class="tab" onclick="showTab('graph',this)">Graphique</button>
  <button class="tab" onclick="showTab('config',this)">Configuration</button>
  <button class="tab" onclick="showTab('ota',this)">Mise a jour</button>
  <button class="tab" onclick="showTab('aide',this)">Aide</button>
</nav>
<main>

<!-- ══ Statut ═══════════════════════════════════════════════════════════════ -->
<div id="pane-status">
  <div class="cards">
    <div class="card">
      <div class="card-hd grid" id="card-grid-name">RESEAU</div>
      <div class="status-row"><span class="dot" id="gd"></span><span id="gs">--</span></div>
      <div class="big" id="gp">--</div>
      <div class="sub" id="gk">--</div>
      <div class="tag" id="gt">--</div>
    </div>
    <div class="card">
      <div class="card-hd solar" id="card-solar-name">SOLAIRE</div>
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
  </div>
  <div class="cards" id="extra-cards"></div>

  <!-- Bilan journalier -->
  <div class="day-grid">
    <div class="si"><div class="si-lbl">Reseau aujourd'hui</div><div class="si-val" id="d-g-kwh">-- kWh</div></div>
    <div class="si"><div class="si-lbl">Solaire aujourd'hui</div><div class="si-val" id="d-s-kwh">-- kWh</div></div>
  </div>

</div>

<!-- ══ Graphique ════════════════════════════════════════════════════════════ -->
<div id="pane-graph" style="display:none">
  <div class="day-grid">
    <div class="si"><div class="si-lbl">Autoconsommation aujourd'hui</div><div class="si-val" id="d-ac">-- %</div></div>
    <div class="si"><div class="si-lbl">Autosuffisance aujourd'hui</div><div class="si-val" id="d-as">-- %</div></div>
  </div>
  <section>
    <h3>DERNIERES 2 HEURES</h3>
    <div style="position:relative;height:260px"><canvas id="ch-2h"></canvas></div>
    <div id="st-2h" style="font-size:12px;color:var(--muted);margin-top:4px;text-align:right"></div>
  </section>
  <section>
    <h3>AUJOURD'HUI (00:00 &mdash; maintenant)</h3>
    <div style="position:relative;height:260px"><canvas id="ch-24h"></canvas></div>
    <div id="st-24h" style="font-size:12px;color:var(--muted);margin-top:4px;text-align:right"></div>
  </section>
</div>

<!-- ══ Configuration ════════════════════════════════════════════════════════ -->
<div id="pane-config" style="display:none">
<div class="ap-banner" id="ap-banner">
  Point d'acces WiFi actif &mdash; <strong id="ap-banner-ip">192.168.4.1</strong> &mdash; SSID : DashEnergy-Config
</div>

<!-- Mode demo -->
<details><summary><div class="sum-body"><span>MODE DEMO</span><span class="sum-desc">Injecte des valeurs fictives — aucun appareil reel interroge</span></div></summary>
<div class="acc-body">
<div class="info-box">Injecte des valeurs fictives animees &mdash; aucun appareil reel n'est interroge. Utile pour presenter le tableau de bord sans connexion aux appareils.</div>
<div style="display:flex;align-items:center;gap:16px;flex-wrap:wrap">
  <button type="button" id="demo-btn" onclick="toggleDemo()" class="btn btn-scan" style="min-width:180px">Chargement...</button>
  <span id="demo-st" style="font-size:13px;color:var(--muted)"></span>
</div>
</div></details>

<!-- WiFi (formulaire independant) -->
<form id="wifi-form" onsubmit="saveWifi(event)">
<details id="sec-wifi"><summary><div class="sum-body"><span>WIFI</span><span class="sum-desc">Connexion sans-fil et adresse IP</span></div></summary>
<div class="acc-body">
<div class="info-box" id="wifi-current">WiFi actuel : chargement...</div>
<div class="row-scan">
  <button type="button" class="btn btn-scan" onclick="startScan()">Scanner les reseaux</button>
  <span id="scan-status" style="font-size:13px;color:var(--muted)"></span>
</div>
<label><span class="lbl">Reseau WiFi (SSID)</span>
  <input list="net-list" name="wifi_ssid" placeholder="Nom du reseau" autocomplete="off">
  <datalist id="net-list"></datalist>
</label>
<label><span class="lbl">Mot de passe WiFi</span>
  <input name="wifi_pass" type="password" placeholder="Laisser vide pour ne pas changer">
</label>
<div class="radio-row">
  <label><input type="radio" name="wifi_dhcp" value="1" onchange="updWifi()"> DHCP</label>
  <label><input type="radio" name="wifi_dhcp" value="0" onchange="updWifi()"> IP fixe</label>
</div>
<div id="static-fields" style="display:none">
  <div class="two">
    <label><span class="lbl">Adresse IP</span><input name="static_ip" placeholder="192.168.1.100"></label>
    <label><span class="lbl">Passerelle</span><input name="static_gw" placeholder="192.168.1.1"></label>
  </div>
  <div class="two">
    <label><span class="lbl">Masque sous-reseau</span><input name="static_nm" placeholder="255.255.255.0"></label>
    <label><span class="lbl">DNS</span><input name="static_dns" placeholder="8.8.8.8"></label>
  </div>
</div>
<div class="actions"><button type="submit" class="btn btn-save">Enregistrer WiFi et redemarrer</button></div>
</div></details>
</form>

<!-- Formulaire principal appareils -->
<form id="cfg" onsubmit="saveCfg(event)">

<details><summary><div class="sum-body"><span>GENERAL</span><span class="sum-desc">Nom du tableau de bord, orientation ecran, fuseau horaire</span></div></summary>
<div class="acc-body">
<div class="two">
  <label><span class="lbl">Nom du tableau de bord</span>
    <input name="device_name" maxlength="31" placeholder="Dash Energy">
  </label>
  <label><span class="lbl">Orientation ecran</span>
    <select name="display_rotation">
      <option value="0">Normal (0 deg)</option>
      <option value="2">Retourne (180 deg)</option>
    </select>
  </label>
</div>
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
</div></details>

<details><summary><div class="sum-body"><span>APPAREILS RESEAU</span><span class="sum-desc">Bibliotheque d'adresses IP — selectionnable dans chaque source</span></div><span class="sec-badge" id="host-badge">0</span></summary>
<div class="acc-body">
<div id="host-list"></div>
<div style="display:flex;gap:10px;flex-wrap:wrap;margin-top:4px">
  <button type="button" class="btn btn-scan" id="host-add-btn" onclick="addHost()">+ Ajouter</button>
  <button type="button" class="btn btn-save" onclick="saveHosts()">Enregistrer sans redemarrer</button>
</div>
</div></details>

<details><summary><div class="sum-body"><span>RESEAU ELECTRIQUE &mdash; <span id="sec-grid-lbl" style="color:var(--accent)">--</span></span><span class="sum-desc">Source de mesure du courant electrique</span></div></summary>
<div class="acc-body">
<div class="two">
  <label><span class="lbl">Nom affiche</span>
    <input name="grid_name" id="grid-name-inp" maxlength="31" placeholder="Reseau" oninput="updSecLabels()">
  </label>
  <label><span class="lbl">Source</span>
    <select name="grid_device" onchange="updGrid()">
      <option value="0">-- Non configure --</option>
      <optgroup label="Shelly Gen1">
        <option value="1">Shelly EM Gen1 &mdash; 1 sonde</option>
        <option value="2">Shelly EM Gen1 &mdash; 2 sondes</option>
        <option value="3">Shelly 3EM &mdash; 1 phase</option>
        <option value="4">Shelly 3EM &mdash; 2 phases</option>
        <option value="5">Shelly 3EM &mdash; 3 phases</option>
      </optgroup>
      <optgroup label="Shelly Gen2/Gen3">
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
</div>
<div id="grid-ip-wrap" class="host-row">
  <label><span class="lbl">Adresse IP / Hote</span>
    <input name="grid_host" id="grid-host-inp" placeholder="192.168.1.x">
  </label>
  <select class="host-picker" onchange="applyHostPick(this,'grid-host-inp')"></select>
</div>
<div id="grid-ha-fields" style="display:none">
  <label><span class="lbl">Entite puissance reseau (W)</span>
    <input name="grid_entity" placeholder="sensor.linky_power" maxlength="63">
  </label>
  <label><span class="lbl">Entite energie reseau aujourd'hui (kWh) &mdash; optionnel</span>
    <input name="grid_energy_entity" placeholder="sensor.linky_energy_today" maxlength="63">
  </label>
</div>
</div></details>

<details><summary><div class="sum-body"><span>SOLAIRE &mdash; <span id="sec-solar-lbl" style="color:var(--orange)">--</span></span><span class="sum-desc">Sources PV — jusqu'a 4 sources independantes</span></div><span class="sec-badge" id="sol-badge">0</span></summary>
<div class="acc-body">
<label><span class="lbl">Nom affiche (agregat)</span>
  <input name="solar_name" id="solar-name-inp" maxlength="31" placeholder="Solaire" oninput="updSecLabels()">
</label>
<div id="sol-list"></div>
<button type="button" class="btn btn-scan" id="sol-add-btn" onclick="addSol()">+ Ajouter une source</button>
</div></details>

<details><summary><div class="sum-body"><span>BATTERIES</span><span class="sum-desc">Stockage d'energie — puissance et etat de charge</span></div><span class="sec-badge" id="bat-badge">0</span></summary>
<div class="acc-body">
<div id="bat-list"></div>
<button type="button" class="btn btn-scan" id="bat-add-btn" onclick="addBat()">+ Ajouter une batterie</button>
</div></details>

<details><summary><div class="sum-body"><span>ROUTEURS SOLAIRES</span><span class="sum-desc">Derivation du surplus solaire vers une charge</span></div><span class="sec-badge" id="rtr-badge">0</span></summary>
<div class="acc-body">
<div id="rtr-list"></div>
<button type="button" class="btn btn-scan" id="rtr-add-btn" onclick="addRtr()">+ Ajouter un routeur</button>
</div></details>

<div id="ha-section" style="display:none">
<details><summary><div class="sum-body"><span>HOME ASSISTANT &mdash; TOKEN</span><span class="sum-desc">Bearer token pour l'API REST HA (Profil → Securite → Jetons)</span></div></summary>
<div class="acc-body">
<div class="info-box">Token genere dans HA : Profil &rarr; Securite &rarr; Jetons d'acces longue duree.</div>
<label><span class="lbl">Bearer Token</span>
  <input name="ha_token" type="password" maxlength="191" placeholder="eyJhbGc...">
</label>
</div></details>
</div>

<div class="actions" style="padding:4px 0 8px">
  <button type="submit" class="btn btn-save">Enregistrer et redemarrer</button>
  <button type="button" class="btn btn-restart" onclick="restart()">Redemarrer</button>
</div>
</form>

<!-- MQTT -->
<form id="mqtt-form" onsubmit="saveMqtt(event)">
<details><summary><div class="sum-body"><span>MQTT &amp; HOME ASSISTANT AUTO-DECOUVERTE</span><span class="sum-desc">Publication des donnees vers un broker MQTT + capteurs HA</span></div></summary>
<div class="acc-body">
<div class="info-box">Publie les donnees sur un broker MQTT. Active l'auto-decouverte HA pour voir les capteurs directement.</div>
<div style="display:flex;gap:24px;align-items:center;margin-bottom:14px;flex-wrap:wrap">
  <label style="display:flex;align-items:center;gap:8px;cursor:pointer;font-size:14px">
    <input type="checkbox" name="mqtt_enabled" style="width:16px;height:16px;margin:0;flex-shrink:0">
    <span>Activer MQTT</span>
  </label>
  <label style="display:flex;align-items:center;gap:8px;cursor:pointer;font-size:14px">
    <input type="checkbox" name="mqtt_ha" style="width:16px;height:16px;margin:0;flex-shrink:0">
    <span>Auto-decouverte HA</span>
  </label>
</div>
<div class="two">
  <label><span class="lbl">Adresse du broker</span><input name="mqtt_host" placeholder="192.168.1.x"></label>
  <label><span class="lbl">Port</span><input name="mqtt_port" type="number" min="1" max="65535" placeholder="1883"></label>
</div>
<div class="two">
  <label><span class="lbl">Utilisateur</span><input name="mqtt_user" placeholder="homeassistant"></label>
  <label><span class="lbl">Mot de passe</span><input name="mqtt_pass" type="password"></label>
</div>
<label><span class="lbl">Topic de base</span>
  <input name="mqtt_topic" placeholder="dashenergy" maxlength="31">
</label>
<div class="actions"><button type="submit" class="btn btn-save">Enregistrer MQTT et redemarrer</button></div>
</div></details>
</form>

<!-- Systeme -->
<details open><summary>SYSTEME</summary>
<div class="acc-body">
<div class="sys">
  <div class="si"><div class="si-lbl">Adresse IP</div><div class="si-val" id="i-ip">--</div></div>
  <div class="si"><div class="si-lbl">Reseau WiFi</div><div class="si-val" id="i-ssid">--</div></div>
  <div class="si"><div class="si-lbl">Signal WiFi</div><div class="si-val" id="i-rssi">--</div></div>
  <div class="si"><div class="si-lbl">Uptime</div><div class="si-val" id="i-up">--</div></div>
  <div class="si"><div class="si-lbl">RAM interne</div><div class="si-val" id="i-heap">--</div></div>
  <div class="si"><div class="si-lbl">PSRAM</div><div class="si-val" id="i-psram">--</div></div>
  <div class="si"><div class="si-lbl">Carte SD</div><div class="si-val" id="i-sd">--</div></div>
  <div class="si"><div class="si-lbl">CPU</div><div class="si-val" id="i-cpu">--</div></div>
  <div class="si"><div class="si-lbl">Firmware</div><div class="si-val" id="i-build">--</div></div>
  <div class="si"><div class="si-lbl">Heure NTP</div><div class="si-val" id="i-time">--</div></div>
  <div class="si" id="si-ap" style="display:none"><div class="si-lbl">Point d'acces</div><div class="si-val" id="i-ap">--</div></div>
</div>
</div></details>

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

<!-- ══ Aide ══════════════════════════════════════════════════════════════════ -->
<div id="pane-aide" style="display:none">
<style>
.help-h2{font-size:15px;font-weight:700;color:var(--accent);margin:22px 0 8px;border-bottom:1px solid var(--border);padding-bottom:6px}
.help-h3{font-size:13px;font-weight:700;color:var(--text);margin:14px 0 5px}
.help-p{font-size:13px;color:var(--muted);line-height:1.6;margin-bottom:8px}
.help-table{width:100%;border-collapse:collapse;font-size:13px;margin-bottom:12px}
.help-table th{text-align:left;color:var(--accent);font-weight:600;padding:5px 8px;border-bottom:1px solid var(--border)}
.help-table td{padding:5px 8px;border-bottom:1px solid #21262d;color:var(--muted)}
.help-table td:first-child{color:var(--text);font-weight:500;white-space:nowrap}
.help-code{font-family:monospace;font-size:12px;background:#21262d;padding:2px 6px;border-radius:4px;color:var(--orange)}
.help-note{background:#1a2940;border-left:3px solid var(--accent);padding:10px 14px;border-radius:0 6px 6px 0;font-size:12px;color:var(--muted);margin:10px 0}
.help-warn{background:#2d1a00;border-left:3px solid var(--orange);padding:10px 14px;border-radius:0 6px 6px 0;font-size:12px;color:#c9a14a;margin:10px 0}
</style>
<section>

<div class="help-h2">Premier demarrage</div>
<p class="help-p">A la mise sous tension, un point d'acces Wi-Fi <strong>DashEnergy-Config</strong> (sans mot de passe) est cree. Connectez-vous dessus et ouvrez <span class="help-code">http://192.168.4.1/</span> pour acceder a la configuration.</p>
<ol style="font-size:13px;color:var(--muted);padding-left:20px;line-height:2">
<li>Onglet <strong>Configuration</strong> → section <strong>Wi-Fi</strong> → entrez votre SSID + mot de passe → <strong>Sauvegarder</strong></li>
<li>L'ESP redemarre et se connecte a votre reseau — son IP est affichee sur le port serie</li>
<li>Acces futur via <span class="help-code">http://&lt;IP&gt;/</span> ou <span class="help-code">http://dashenergy.local/</span></li>
<li>Configurez vos appareils (Reseau, Solaire, Batteries, Routeurs) et sauvegardez</li>
</ol>
<div class="help-note">Si le Wi-Fi est perdu, le point d'acces se remet en route automatiquement et l'ESP retente la connexion toutes les 30 secondes.</div>

<div class="help-h2">Navigation entre ecrans</div>
<table class="help-table">
<tr><th>Methode</th><th>Comment</th></tr>
<tr><td>Tactile — ecran principal</td><td>Touchez une carte pour aller sur la page de detail</td></tr>
<tr><td>Tactile — page de detail</td><td>Touchez ← Retour pour revenir a l'ecran principal</td></tr>
<tr><td>Interface web</td><td>Boutons de navigation dans l'onglet Statut</td></tr>
<tr><td>API</td><td><span class="help-code">POST /api/navigate</span> avec <span class="help-code">{"screen":0}</span> (0=principal, 1=solaire, 2=reseau, 3=batterie, 4=routeur)</td></tr>
</table>
<div class="help-note">Retour automatique a l'ecran principal apres <strong>30 secondes</strong> d'inactivite sur une page de detail.</div>

<div class="help-h2">Configuration des appareils</div>

<div class="help-h3">Reseau electrique</div>
<table class="help-table">
<tr><th>Type</th><th>Champ « Adresse »</th></tr>
<tr><td>Shelly EM / 3EM / Pro</td><td>IP de la Shelly (ex : 192.168.1.50)</td></tr>
<tr><td>F1ATB</td><td>IP du routeur F1ATB</td></tr>
<tr><td>Home Assistant</td><td>IP:port du serveur HA (ex : 192.168.1.10:8123)</td></tr>
</table>
<p class="help-p">Pour Home Assistant, renseignez le <strong>Token HA</strong> dans la section dediee (Profil HA → Securite → Jetons d'acces longue duree).</p>
<div class="help-warn">Puissance positive = import (vous achetez). Puissance negative = export (vous revendez). Ajustez votre entite HA si necessaire.</div>

<div class="help-h3">Solaire (jusqu'a 4 sources)</div>
<table class="help-table">
<tr><th>Type</th><th>Champ « Adresse »</th><th>Champ specifique</th></tr>
<tr><td>OpenDTU</td><td>IP du DTU</td><td>Numero de serie onduleur</td></tr>
<tr><td>AhoyDTU</td><td>IP du DTU</td><td>Numero de serie onduleur</td></tr>
<tr><td>Fronius</td><td>IP de l'onduleur</td><td>—</td></tr>
<tr><td>Shelly Plug / EM / 3EM</td><td>IP de la Shelly</td><td>—</td></tr>
<tr><td>Home Assistant</td><td>IP:port HA</td><td>Entite puissance + entite energie (optionnel)</td></tr>
</table>
<p class="help-p">La puissance max (W) sert a calibrer la barre de progression sur l'ecran principal.</p>

<div class="help-h3">Batteries (jusqu'a 4)</div>
<table class="help-table">
<tr><th>Type</th><th>Adresse</th><th>Entites requises</th></tr>
<tr><td>ESPHome JK-BMS</td><td>IP de l'ESP ESPHome</td><td>Automatique (web_server)</td></tr>
<tr><td>Home Assistant</td><td>IP:port HA</td><td>Puissance (W) + SOC (%)</td></tr>
</table>

<div class="help-h3">Routeurs solaires (jusqu'a 4)</div>
<table class="help-table">
<tr><th>Type</th><th>Entites HA sugginees</th></tr>
<tr><td>F1ATB</td><td>Adresse IP du F1ATB — puissance + duree automatiques</td></tr>
<tr><td>Home Assistant</td><td>Puissance (W), energie (kWh), duree (h), triac (%)</td></tr>
</table>

<div class="help-h2">Carte SD</div>
<p class="help-p">La carte SD (<strong>FAT32</strong>) conserve l'energie hebdomadaire et mensuelle entre les redemarrages. Sans carte SD, ces donnees sont perdues a chaque coupure de courant.</p>
<table class="help-table">
<tr><th>Fichier</th><th>Contenu</th></tr>
<tr><td><span class="help-code">daily.bin</span></td><td>Baselines journalieres reseau + solaire</td></tr>
<tr><td><span class="help-code">period.bin</span></td><td>Bases hebdo/mensuelles tous appareils</td></tr>
<tr><td><span class="help-code">day_ring.bin</span></td><td>Courbe journaliere (288 pts × 5 min)</td></tr>
<tr><td><span class="help-code">log.csv</span></td><td>Historique CSV horodate</td></tr>
</table>

<div class="help-h2">Mise a jour firmware (OTA)</div>
<p class="help-p">Allez dans l'onglet <strong>Mise a jour</strong>, selectionnez le fichier <span class="help-code">.pio/build/jc8048w550/firmware.bin</span> et cliquez <strong>Flasher</strong>. L'ESP redemarrera automatiquement.</p>

<div class="help-h2">Depannage rapide</div>
<table class="help-table">
<tr><th>Probleme</th><th>Solution</th></tr>
<tr><td>Ecran blanc</td><td>Verifiez que le firmware jc8048w550 est flash. Regardez le port serie.</td></tr>
<tr><td>Tactile inversé</td><td>Changez l'orientation 0 ↔ 2 dans Configuration → General</td></tr>
<tr><td>Aucune donnee</td><td>Activez le mode demo pour tester l'affichage. Verifiez l'IP de chaque appareil.</td></tr>
<tr><td>Energie repart de zero</td><td>Normal sans SD. Avec SD : verifiez que sd_ready=true dans /api/status</td></tr>
<tr><td>Wi-Fi introuvable</td><td>Verifiez que c'est un reseau 2.4 GHz (le 5 GHz n'est pas supporte)</td></tr>
<tr><td>Interface web lente</td><td>Redemarrez via Configuration → Redemarrer l'ESP</td></tr>
</table>

<div class="help-h2">A propos</div>
<p class="help-p">Dash Energy <span id="aide-ver"></span> — firmware open-source pour ESP32-S3 JC8048W550</p>
<p class="help-p">Code source et documentation : <strong>github.com/Alex77138/Energy-Dashboard</strong></p>
<script>document.getElementById('aide-ver').textContent=document.querySelector('.ver')?document.querySelector('.ver').textContent:'';</script>

</section>
</div><!-- pane-aide -->

</main>
<div class="toast" id="toast"></div>
<script>
var tid=null, dayTid=null, chDay=null, ch2h=null, ch24h=null, graphTid=null;

function showTab(name,btn){
  document.querySelectorAll('.tab').forEach(function(b){b.classList.remove('active');});
  btn.classList.add('active');
  ['status','graph','config','ota','aide'].forEach(function(p){
    document.getElementById('pane-'+p).style.display=(p===name?'':'none');
  });
  if(name==='status'){
    clearInterval(graphTid);graphTid=null;
    startRefresh();
  } else if(name==='graph'){
    clearInterval(tid);tid=null;
    clearInterval(dayTid);dayTid=null;
    clearInterval(graphTid);graphTid=null;
    fetchCharts();
    graphTid=setInterval(fetchCharts,300000);
  } else {
    clearInterval(tid);tid=null;
    clearInterval(dayTid);dayTid=null;
    clearInterval(graphTid);graphTid=null;
    if(name==='config'){loadCfg();fetchStatus();if(!tid)tid=setInterval(fetchStatus,3000);}
  }
}

function updGrid(){
  var v=parseInt(document.querySelector('[name=grid_device]').value);
  var isHa=(v===9);
  document.getElementById('grid-ha-fields').style.display=isHa?'':'none';
  updHaSection();
}
/* ── Multi-items : batteries / routeurs / hotes / solaires ── */
var batState=[], rtrState=[], hostState=[], solarState=[];

function hpOpts(){
  var s='<option value="">-- Choisir --</option>';
  hostState.forEach(function(h){if(h.name||h.ip)s+='<option value="'+esc(h.ip)+'">'+esc(h.name||h.ip)+'</option>';});
  return s;
}
function rebuildPickers(){
  var o=hpOpts();
  document.querySelectorAll('.host-picker').forEach(function(s){s.innerHTML=o;});
}
function applyHostPick(sel,fid){
  var ip=sel.value;
  if(ip){var el=document.getElementById(fid);if(el)el.value=ip;}
  sel.value='';
}
function esc(s){return (s||'').replace(/&/g,'&amp;').replace(/"/g,'&quot;').replace(/</g,'&lt;');}

function updHaSection(){
  var gv=parseInt(document.querySelector('[name=grid_device]').value);
  var needHa=(gv===9);
  if(!needHa)(solarState||[]).forEach(function(s){if(s.device==10)needHa=true;});
  if(!needHa)(batState||[]).forEach(function(b){if(b.device==2)needHa=true;});
  if(!needHa)(rtrState||[]).forEach(function(r){if(r.device==1)needHa=true;});
  document.getElementById('ha-section').style.display=needHa?'':'none';
}

/* ── Solaires (multi-source) ── */
function solHtml(s,i){
  var devs=[
    {v:0,l:'-- Non configure --'},
    {v:1,l:'OpenDTU'},{v:2,l:'AhoyDTU'},
    {v:3,l:'Shelly Plug Gen1'},{v:4,l:'Shelly Plug S/Plus Gen2/3'},
    {v:5,l:'Shelly EM Gen1 &mdash; 1 sonde'},{v:6,l:'Shelly EM Gen1 &mdash; 2 sondes'},
    {v:7,l:'Shelly 3EM &mdash; 1 phase'},{v:8,l:'Shelly 3EM &mdash; 2 phases'},{v:9,l:'Shelly 3EM &mdash; 3 phases'},
    {v:10,l:'Home Assistant'},{v:11,l:'Fronius'}
  ];
  var sel='<select onchange="updSolItem('+i+')">';
  devs.forEach(function(o){sel+='<option value="'+o.v+'"'+(s.device==o.v?' selected':'')+'>'+o.l+'</option>';});
  sel+='</select>';
  var isDtu=(s.device==1||s.device==2);var isHa=(s.device==10);var hasIp=(s.device>0&&!isHa);
  return '<div class="multi-item" id="sol-item-'+i+'">'
    +'<div class="multi-hd"><span id="sname-hd-'+i+'">'+(s.name||('Source '+(i+1)))+'</span>'
    +'<button type="button" class="btn-del" onclick="removeSol('+i+')">Supprimer</button></div>'
    +'<div class="two"><label><span class="lbl">Nom</span>'
    +'<input id="sname-'+i+'" value="'+esc(s.name||'')+'" placeholder="Source '+(i+1)+'" maxlength="31" oninput="liveSolName('+i+')">'
    +'</label><label><span class="lbl">Type</span>'+sel+'</label></div>'
    +'<label><span class="lbl">Puissance crete (Wc) &mdash; jauge ecran</span>'
    +'<input id="smaxw-'+i+'" type="number" min="0" max="99999" value="'+(s.max_w||0)+'" placeholder="0 = desactivee"></label>'
    +'<div id="sip-'+i+'" style="display:'+(hasIp?'':'none')+'" class="host-row">'
    +'<label><span class="lbl">Adresse IP / Hote</span><input id="shost-'+i+'" value="'+esc(s.host||'')+'" placeholder="192.168.1.x"></label>'
    +'<select class="host-picker" onchange="applyHostPick(this,\'shost-'+i+'\')">'+hpOpts()+'</select></div>'
    +'<div id="sha-'+i+'" style="display:'+(isHa?'':'none')+'">'
    +'<div class="host-row"><label><span class="lbl">Adresse Home Assistant (host:port)</span>'
    +'<input id="shahost-'+i+'" value="'+esc(s.host||'')+'" placeholder="192.168.1.x:8123"></label>'
    +'<select class="host-picker" onchange="applyHostPick(this,\'shahost-'+i+'\')">'+hpOpts()+'</select></div>'
    +'<label><span class="lbl">Entite puissance (W)</span><input id="sent-'+i+'" value="'+esc(s.entity||'')+'" placeholder="sensor.solar_power" maxlength="63"></label>'
    +'<label><span class="lbl">Entite energie aujourd\'hui (kWh)</span><input id="senent-'+i+'" value="'+esc(s.energy_entity||'')+'" placeholder="sensor.solar_energy_today" maxlength="63"></label>'
    +'</div>'
    +'<div id="sdtu-'+i+'" style="display:'+(isDtu?'':'none')+'">'
    +'<div id="sdtu-auth-'+i+'" style="display:'+(s.device==1?'':'none')+'" class="two">'
    +'<label><span class="lbl">Utilisateur</span><input id="suser-'+i+'" value="'+esc(s.user||'admin')+'"></label>'
    +'<label><span class="lbl">Mot de passe</span><input id="spass-'+i+'" type="password" value="'+esc(s.pass||'')+'"></label></div>'
    +'<label><span class="lbl">Numero de serie onduleur</span><input id="sser-'+i+'" value="'+esc(s.serial||'')+'" placeholder="114181234567" maxlength="63"></label>'
    +'</div></div>';
}
function liveSolName(i){
  var v=(document.getElementById('sname-'+i)||{}).value||'Source '+(i+1);
  var hd=document.getElementById('sname-hd-'+i);if(hd)hd.textContent=v;
}
function renderSolList(){
  var h='';solarState.forEach(function(s,i){h+=solHtml(s,i);});
  document.getElementById('sol-list').innerHTML=h;
  document.getElementById('sol-add-btn').style.display=solarState.length>=4?'none':'';
  document.getElementById('sol-badge').textContent=solarState.length;
  updHaSection();rebuildPickers();
}
function updSolItem(i){
  var v=parseInt(document.querySelector('#sol-item-'+i+' select').value);
  solarState[i].device=v;
  var isDtu=(v==1||v==2);var isHa=(v==10);var hasIp=(v>0&&!isHa);
  document.getElementById('sip-'+i).style.display=hasIp?'':'none';
  document.getElementById('sha-'+i).style.display=isHa?'':'none';
  document.getElementById('sdtu-'+i).style.display=isDtu?'':'none';
  if(isDtu)document.getElementById('sdtu-auth-'+i).style.display=v==1?'':'none';
  updHaSection();
}
function addSol(){
  if(solarState.length>=4)return;
  solarState.push({device:0,name:'',host:'',max_w:0,user:'admin',pass:'',serial:'',entity:'',energy_entity:''});
  renderSolList();
}
function removeSol(i){solarState.splice(i,1);renderSolList();}
function readSolState(){solarState.forEach(function(s,i){
  s.name=(document.getElementById('sname-'+i)||{}).value||'';
  s.device=parseInt((document.querySelector('#sol-item-'+i+' select')||{}).value)||0;
  s.max_w=parseInt((document.getElementById('smaxw-'+i)||{}).value)||0;
  var isHa=(s.device==10);var isDtu=(s.device==1||s.device==2);
  if(isHa){
    s.host=(document.getElementById('shahost-'+i)||{}).value||'';
    s.entity=(document.getElementById('sent-'+i)||{}).value||'';
    s.energy_entity=(document.getElementById('senent-'+i)||{}).value||'';
  }else{
    s.host=(document.getElementById('shost-'+i)||{}).value||'';
    s.entity='';s.energy_entity='';
  }
  if(isDtu){
    s.user=(document.getElementById('suser-'+i)||{}).value||'admin';
    s.pass=(document.getElementById('spass-'+i)||{}).value||'';
    s.serial=(document.getElementById('sser-'+i)||{}).value||'';
  }
});}

function updSecLabels(){
  var gn=document.getElementById('grid-name-inp');
  var sn=document.getElementById('solar-name-inp');
  if(gn)document.getElementById('sec-grid-lbl').textContent=gn.value||'Reseau';
  if(sn)document.getElementById('sec-solar-lbl').textContent=sn.value||'Solaire';
}

/* ── Hotes ── */
function hostHtml(h,i){
  return '<div class="multi-item" id="host-item-'+i+'">'
    +'<div class="multi-hd"><span>'+(h.name||('Appareil '+(i+1)))+'</span>'
    +'<button type="button" class="btn-del" onclick="removeHost('+i+')">Supprimer</button></div>'
    +'<div class="two">'
    +'<label><span class="lbl">Nom</span><input id="hname-'+i+'" value="'+esc(h.name||'')+'" placeholder="Home Assistant" maxlength="31" oninput="liveHostName('+i+')"></label>'
    +'<label><span class="lbl">Adresse IP / Hote:port</span><input id="hip-'+i+'" value="'+esc(h.ip||'')+'" placeholder="192.168.1.x:8123" maxlength="63"></label>'
    +'</div></div>';
}
function liveHostName(i){
  var v=(document.getElementById('hname-'+i)||{}).value||'Appareil '+(i+1);
  var hd=document.querySelector('#host-item-'+i+' .multi-hd span');
  if(hd)hd.textContent=v;
  readHostState();rebuildPickers();
}
function renderHostList(){
  var h='';hostState.forEach(function(x,i){h+=hostHtml(x,i);});
  document.getElementById('host-list').innerHTML=h;
  var n=hostState.length;
  document.getElementById('host-add-btn').style.display=n>=8?'none':'';
  document.getElementById('host-badge').textContent=n;
  rebuildPickers();
}
function addHost(){if(hostState.length>=8)return;hostState.push({name:'',ip:''});renderHostList();}
function removeHost(i){hostState.splice(i,1);renderHostList();}
function readHostState(){hostState.forEach(function(h,i){h.name=(document.getElementById('hname-'+i)||{}).value||'';h.ip=(document.getElementById('hip-'+i)||{}).value||'';});}

/* ── Batteries ── */
function batHtml(b,i){
  var sel='<select onchange="updBatItem('+i+')">';
  [{v:0,l:'-- Non configure --'},{v:1,l:'JK-BMS via ESPHome'},{v:2,l:'Home Assistant'}].forEach(function(o){sel+='<option value="'+o.v+'"'+(b.device==o.v?' selected':'')+'>'+o.l+'</option>';});
  sel+='</select>';
  var bname=b.name||(i>0?'Batterie '+(i+1):'');
  return '<div class="multi-item" id="bat-item-'+i+'">'
    +'<div class="multi-hd"><span id="bname-hd-'+i+'">'+(b.name||('Batterie '+(i+1)))+'</span>'
    +'<button type="button" class="btn-del" onclick="removeBat('+i+')">Supprimer</button></div>'
    +'<div class="two"><label><span class="lbl">Nom affiche</span>'
    +'<input id="bname-'+i+'" value="'+esc(bname)+'" placeholder="Batterie '+(i+1)+'" maxlength="31" oninput="liveBatName('+i+')">'
    +'</label><label><span class="lbl">Source</span>'+sel+'</label></div>'
    +'<div id="bh-'+i+'" style="display:'+(b.device>0?'':'none')+'">'
    +'<div class="host-row"><label><span class="lbl" id="bhl-'+i+'">'+(b.device==1?'Adresse IP ESPHome':'Adresse Home Assistant (host:port)')+'</span>'
    +'<input id="bhost-'+i+'" value="'+esc(b.host||'')+'" placeholder="'+(b.device==1?'192.168.1.x':'192.168.1.x:8123')+'"></label>'
    +'<select class="host-picker" onchange="applyHostPick(this,\'bhost-'+i+'\')">'+hpOpts()+'</select></div></div>'
    +'<div id="bha-'+i+'" style="display:'+(b.device==2?'':'none')+'">'
    +'<label><span class="lbl">Entite puissance (W)</span><input id="bpwr-'+i+'" value="'+esc(b.power_entity||'')+'" placeholder="sensor.battery_power" maxlength="63"></label>'
    +'<label><span class="lbl">Entite SoC (%)</span><input id="bsoc-'+i+'" value="'+esc(b.soc_entity||'')+'" placeholder="sensor.battery_soc" maxlength="63"></label>'
    +'</div></div>';
}
function liveBatName(i){
  var v=(document.getElementById('bname-'+i)||{}).value||'Batterie '+(i+1);
  var hd=document.getElementById('bname-hd-'+i);if(hd)hd.textContent=v;
}
function renderBatList(){
  var h='';batState.forEach(function(b,i){h+=batHtml(b,i);});
  document.getElementById('bat-list').innerHTML=h;
  document.getElementById('bat-add-btn').style.display=batState.length>=4?'none':'';
  document.getElementById('bat-badge').textContent=batState.length;
  updHaSection();
}
function updBatItem(i){
  var v=parseInt(document.querySelector('#bat-item-'+i+' select').value);
  batState[i].device=v;
  document.getElementById('bh-'+i).style.display=v>0?'':'none';
  document.getElementById('bha-'+i).style.display=v==2?'':'none';
  var lbl=document.getElementById('bhl-'+i);
  if(lbl)lbl.textContent=v==1?'Adresse IP ESPHome':'Adresse Home Assistant (host:port)';
  updHaSection();
}
function addBat(){if(batState.length>=4)return;batState.push({device:0,name:'',host:'',power_entity:'',soc_entity:''});renderBatList();}
function removeBat(i){batState.splice(i,1);renderBatList();}
function readBatState(){batState.forEach(function(b,i){
  b.name=(document.getElementById('bname-'+i)||{}).value||'';
  b.device=parseInt((document.querySelector('#bat-item-'+i+' select')||{}).value)||0;
  b.host=(document.getElementById('bhost-'+i)||{}).value||'';
  b.power_entity=(document.getElementById('bpwr-'+i)||{}).value||'';
  b.soc_entity=(document.getElementById('bsoc-'+i)||{}).value||'';
});}

/* ── Routeurs ── */
function rtrHtml(r,i){
  var sel='<select onchange="updRtrItem('+i+')">';
  [{v:0,l:'-- Non configure --'},{v:1,l:'Home Assistant'}].forEach(function(o){sel+='<option value="'+o.v+'"'+(r.device==o.v?' selected':'')+'>'+o.l+'</option>';});
  sel+='</select>';
  return '<div class="multi-item" id="rtr-item-'+i+'">'
    +'<div class="multi-hd"><span id="rname-hd-'+i+'">'+(r.name||('Routeur '+(i+1)))+'</span>'
    +'<button type="button" class="btn-del" onclick="removeRtr('+i+')">Supprimer</button></div>'
    +'<div class="two"><label><span class="lbl">Nom affiche</span>'
    +'<input id="rname-'+i+'" value="'+esc(r.name||'')+'" placeholder="Routeur '+(i+1)+'" maxlength="31" oninput="liveRtrName('+i+')">'
    +'</label><label><span class="lbl">Source</span>'+sel+'</label></div>'
    +'<div id="rha-'+i+'" style="display:'+(r.device==1?'':'none')+'">'
    +'<div class="host-row"><label><span class="lbl">Adresse Home Assistant (host:port)</span>'
    +'<input id="rhost-'+i+'" value="'+esc(r.host||'')+'" placeholder="192.168.1.x:8123"></label>'
    +'<select class="host-picker" onchange="applyHostPick(this,\'rhost-'+i+'\')">'+hpOpts()+'</select></div>'
    +'<label><span class="lbl">Entite puissance routee (W)</span><input id="rpwr-'+i+'" value="'+esc(r.power_entity||'')+'" placeholder="sensor.f1atb_power" maxlength="63"></label>'
    +'<label><span class="lbl">Entite energie aujourd\'hui (kWh) &mdash; optionnel</span><input id="ren-'+i+'" value="'+esc(r.energy_entity||'')+'" placeholder="sensor.f1atb_energy_today" maxlength="63"></label>'
    +'<label><span class="lbl">Entite actif/inactif (binary_sensor) &mdash; optionnel</span><input id="ract-'+i+'" value="'+esc(r.active_entity||'')+'" placeholder="binary_sensor.f1atb_active" maxlength="63"></label>'
    +'<label><span class="lbl">Entite duree equivalente (h decimales) &mdash; optionnel</span><input id="rdur-'+i+'" value="'+esc(r.duration_entity||'')+'" placeholder="sensor.f1atb_duration_h" maxlength="63"></label>'
    +'<label><span class="lbl">Entite ouverture triac (0-100 %) &mdash; optionnel</span><input id="rtri-'+i+'" value="'+esc(r.triac_entity||'')+'" placeholder="sensor.f1atb_triac_pct" maxlength="63"></label>'
    +'</div></div>';
}
function liveRtrName(i){
  var v=(document.getElementById('rname-'+i)||{}).value||'Routeur '+(i+1);
  var hd=document.getElementById('rname-hd-'+i);if(hd)hd.textContent=v;
}
function renderRtrList(){
  var h='';rtrState.forEach(function(r,i){h+=rtrHtml(r,i);});
  document.getElementById('rtr-list').innerHTML=h;
  document.getElementById('rtr-add-btn').style.display=rtrState.length>=4?'none':'';
  document.getElementById('rtr-badge').textContent=rtrState.length;
  updHaSection();
}
function updRtrItem(i){
  var v=parseInt(document.querySelector('#rtr-item-'+i+' select').value);
  rtrState[i].device=v;
  document.getElementById('rha-'+i).style.display=v==1?'':'none';
  updHaSection();
}
function addRtr(){if(rtrState.length>=4)return;rtrState.push({device:0,name:'',host:'',power_entity:'',energy_entity:'',active_entity:'',duration_entity:'',triac_entity:''});renderRtrList();}
function removeRtr(i){rtrState.splice(i,1);renderRtrList();}
function readRtrState(){rtrState.forEach(function(r,i){
  r.name=(document.getElementById('rname-'+i)||{}).value||'';
  r.device=parseInt((document.querySelector('#rtr-item-'+i+' select')||{}).value)||0;
  r.host=(document.getElementById('rhost-'+i)||{}).value||'';
  r.power_entity=(document.getElementById('rpwr-'+i)||{}).value||'';
  r.energy_entity=(document.getElementById('ren-'+i)||{}).value||'';
  r.active_entity=(document.getElementById('ract-'+i)||{}).value||'';
  r.duration_entity=(document.getElementById('rdur-'+i)||{}).value||'';
  r.triac_entity=(document.getElementById('rtri-'+i)||{}).value||'';
});}
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
function fmtMB(used,total){
  if(!total) return used+' MB';
  var pct=Math.round(used/total*100);
  return used+' / '+total+' MB ('+pct+'%)';
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

    document.getElementById('card-grid-name').textContent=(d.grid_name||'RESEAU').toUpperCase();
    document.getElementById('card-solar-name').textContent=(d.solar_name||'SOLAIRE').toUpperCase();
    if(typeof d.demo_mode!=='undefined'){demoActive=!!d.demo_mode;updDemoBtn();}

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
    var sdEl=document.getElementById('i-sd');
    if(sdEl){
      if(d.sd_total_mb>0)sdEl.textContent=fmtMB(d.sd_used_mb,d.sd_total_mb);
      else sdEl.textContent=d.sd_ready?'Montee (vide)':'Non disponible';
    }
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

    var extra='';
    (d.batteries||[]).forEach(function(b,i){
      var onStr=b.online?'En ligne':'Hors ligne';
      var dotCls='dot '+(b.online?'ok':'err');
      var pw='--',soc='--';
      if(b.online){pw=b.power_w>=0?'+'+fmtW(b.power_w)+' (decharge)':fmtW(b.power_w)+' (charge)';soc='SOC : '+b.soc_pct.toFixed(0)+' %';}
      var nm=(b.name||(d.grid_name?'Batterie '+(i+1):'BATTERIE '+(i+1))).toUpperCase();
      extra+='<div class="card"><div class="card-hd" style="color:#3fb950">'+nm+'</div>'
        +'<div class="status-row"><span class="'+dotCls+'"></span><span>'+onStr+'</span></div>'
        +'<div class="big">'+pw+'</div><div class="sub">'+soc+'</div>'
        +'<div class="tag">'+(b.device||'')+'</div></div>';
    });
    (d.routers||[]).forEach(function(r,i){
      var onStr=r.online?(r.active?'Actif':'En veille'):'Hors ligne';
      var dotCls='dot '+(r.online?(r.active?'ok':''):'err');
      var pw='--',sub='';
      if(r.online){
        pw=r.power_w>0?fmtW(r.power_w):(r.triac_pct>0?Math.round(r.triac_pct)+'%':'--');
        if(r.duration_h>0){var h=Math.floor(r.duration_h),m=Math.round((r.duration_h-h)*60);sub+=pad(h)+'h'+pad(m);}
        if(r.triac_pct>0)sub+=(sub?' &nbsp;':'')+Math.round(r.triac_pct)+'%';
        if(r.today_kwh>0)sub+=(sub?'<br>':'')+r.today_kwh.toFixed(2)+' kWh auj.';
      }
      var nm=(r.name||('ROUTEUR '+(i+1))).toUpperCase();
      extra+='<div class="card"><div class="card-hd" style="color:#f4a429">'+nm+'</div>'
        +'<div class="status-row"><span class="'+dotCls+'"></span><span>'+onStr+'</span></div>'
        +'<div class="big">'+pw+'</div><div class="sub">'+sub+'</div>'
        +'<div class="tag">'+(r.device||'')+'</div></div>';
    });
    document.getElementById('extra-cards').innerHTML=extra;
    updateIndicator(d.grid.power_w, d.solar.power_w);
  }).catch(function(){});
}
function startRefresh(){
  fetchStatus();
  if(!tid) tid=setInterval(fetchStatus,3000);
}

/* ── Graphiques ── */
function todayMidnight(){
  var d=new Date(); d.setHours(0,0,0,0); return d.getTime()/1000;
}
function fmtTs(ts){
  var d=new Date(ts*1000);
  return pad(d.getHours())+':'+pad(d.getMinutes());
}

var chartJsLoaded=false, chartJsLoading=false, chartJsCbs=[];
function loadChartJs(cb){
  if(chartJsLoaded&&typeof Chart!=='undefined'){cb();return;}
  chartJsCbs.push(cb);
  if(chartJsLoading)return;
  chartJsLoading=true;
  var s=document.createElement('script');
  s.src='https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js';
  s.onload=function(){
    chartJsLoaded=true;chartJsLoading=false;
    chartJsCbs.forEach(function(f){f();});chartJsCbs=[];
  };
  s.onerror=function(){
    chartJsLoading=false;chartJsCbs=[];
    ['st-2h','st-24h'].forEach(function(id){
      var el=document.getElementById(id);
      if(el)el.textContent='Chart.js non disponible (connexion internet requise)';
    });
  };
  document.head.appendChild(s);
}

var chartOpts={
  animation:false,
  plugins:{legend:{labels:{color:'#e6edf3'}}},
  scales:{
    x:{ticks:{color:'#8b949e',maxTicksLimit:10},grid:{color:'#30363d'}},
    y:{ticks:{color:'#8b949e'},grid:{color:'#30363d'}}
  }
};

function renderChart(canvasId,statusId,pts,chRef,setter){
  var st=document.getElementById(statusId);
  var canvas=document.getElementById(canvasId);
  if(!canvas)return chRef;
  if(!pts||!pts.length){if(st)st.textContent='Pas encore de donnees';return chRef;}
  var labels=pts.map(function(p){return fmtTs(p.ts);});
  var gp=pts.map(function(p){return p.gp;});
  var sp=pts.map(function(p){return p.sp;});
  if(chRef)chRef.destroy();
  var c=new Chart(canvas,{type:'line',data:{labels:labels,datasets:[
    {label:'Reseau (W)',data:gp,borderColor:'#58a6ff',backgroundColor:'rgba(88,166,255,.15)',fill:true,tension:.3,pointRadius:0,borderWidth:2},
    {label:'Solaire (W)',data:sp,borderColor:'#f4a429',backgroundColor:'rgba(244,164,41,.25)',fill:true,tension:.3,pointRadius:0,borderWidth:2.5}
  ]},options:chartOpts});
  if(st)st.textContent=pts.length+' pts — '+fmtTs(pts[0].ts)+' → '+fmtTs(pts[pts.length-1].ts);
  setter(c);
}

function fetchCharts(){
  loadChartJs(function(){
    fetch('/api/daily').then(function(r){return r.json();}).then(function(data){
      var now=Math.floor(Date.now()/1000);
      var midnight=Math.floor(todayMidnight());
      var all=data.pts||[];

      var el=document.getElementById('d-ac');if(el)el.textContent=data.ac!==undefined?(data.ac+'%'):'-- %';
      el=document.getElementById('d-as');if(el)el.textContent=data.as_!==undefined?(data.as_+'%'):'-- %';

      var pts2h=all.filter(function(p){return p.ts>=now-7200;});
      renderChart('ch-2h','st-2h',pts2h,ch2h,function(c){ch2h=c;});

      var pts24h=all.filter(function(p){return p.ts>=midnight;});
      renderChart('ch-24h','st-24h',pts24h,ch24h,function(c){ch24h=c;});
    }).catch(function(){});
  });
}

/* ── Config ── */
function loadCfg(){
  fetch('/api/config').then(function(r){return r.json();}).then(function(c){
    var f=document.getElementById('cfg');
    f.elements.device_name.value=c.device_name||'';
    f.elements.grid_name.value=c.grid_name||'Réseau';
    f.elements.solar_name.value=c.solar_name||'Solaire';
    f.elements.grid_device.value=c.grid_device||0;
    f.elements.grid_host.value=c.grid_host||'';
    f.elements.display_rotation.value=c.display_rotation||0;
    f.elements.grid_entity.value=c.grid_entity||'';
    f.elements.ha_token.value=c.ha_token||'';
    f.elements.timezone.value=c.timezone||'CET-1CEST,M3.5.0,M10.5.0/3';
    f.elements.grid_energy_entity.value=c.grid_energy_entity||'';
    updGrid();
    updSecLabels();
    solarState=(c.solars||[]).filter(function(s){return s.device>0;});
    hostState=(c.hosts||[]).filter(function(h){return h.name||h.ip;});
    batState=(c.batteries||[]).filter(function(b){return b.device>0;});
    rtrState=(c.routers||[]).filter(function(r){return r.device>0;});
    renderSolList();
    renderHostList();
    renderBatList();
    renderRtrList();

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
  readSolState(); readHostState(); readBatState(); readRtrState();
  var fd=new FormData(e.target),data={};
  fd.forEach(function(v,k){data[k]=v;});
  data.grid_device=parseInt(data.grid_device)||0;
  data.display_rotation=parseInt(data.display_rotation)||0;
  data.ha_token=data.ha_token||'';
  data.grid_entity=data.grid_entity||'';
  data.timezone=data.timezone||'CET-1CEST,M3.5.0,M10.5.0/3';
  data.grid_name=data.grid_name||'Réseau';
  data.solar_name=data.solar_name||'Solaire';
  data.solars=solarState;
  data.hosts=hostState.filter(function(h){return h.name||h.ip;});
  data.batteries=batState;
  data.routers=rtrState;
  data.demo_mode=demoActive;
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

function saveHosts(){
  readHostState();
  var hosts=hostState.filter(function(h){return h.name||h.ip;});
  fetch('/api/hosts',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({hosts:hosts})})
    .then(function(r){
      if(r.ok){toast('Appareils reseau enregistres','ok');renderHostPickers();}
      else toast('Erreur sauvegarde','err');
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

/* ── Mode demo ── */
var demoActive=false;
function updDemoBtn(){
  var btn=document.getElementById('demo-btn');
  var st=document.getElementById('demo-st');
  var badge=document.getElementById('demo-badge');
  if(btn){
    btn.textContent=demoActive?'Desactiver le mode demo':'Activer le mode demo';
    btn.style.cssText=demoActive?'min-width:180px;background:#f4a429;color:#000;border-color:#f4a429':'min-width:180px';
  }
  if(st) st.textContent=demoActive?'Valeurs fictives actives — appareils non interroges':'Mode normal — donnees reelles';
  if(badge) badge.style.display=demoActive?'':'none';
}
async function toggleDemo(){
  try{
    var r=await fetch('/api/demo',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({demo:!demoActive})});
    var d=await r.json();
    demoActive=d.demo;
    updDemoBtn();
    toast(demoActive?'Mode demo actif':'Mode demo desactive','ok');
  }catch(e){toast('Erreur','err');}
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

/* ── Navigation ecran ── */
function navTo(n){
  fetch('/api/navigate',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({screen:n})})
    .then(function(r){if(r.ok)toast('Ecran '+['Principal','Solaire','Reseau','Batterie','Routeur'][n],'ok');else toast('Erreur navigation','err');})
    .catch(function(){toast('Erreur reseau','err');});
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
    SolarData   solar_src[MAX_SOLAR] = {};
    BatteryData batteries[MAX_BATTERIES] = {};
    RouterData  routers[MAX_ROUTERS]     = {};

    if (g_data.mutex && xSemaphoreTake(g_data.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        grid  = g_data.grid;
        solar = g_data.solar;
        memcpy(solar_src, g_data.solar_src, sizeof(solar_src));
        memcpy(batteries, g_data.batteries, sizeof(batteries));
        memcpy(routers,   g_data.routers,   sizeof(routers));
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
    doc["grid_name"]   = g_cfg.grid_name;
    doc["solar_name"]  = g_cfg.solar_name;
    doc["demo_mode"]   = g_cfg.demo_mode;
    doc["sd_ready"]    = sd_ready();
    doc["sd_used_mb"]  = (uint32_t)(sd_used_bytes()  / (1024ULL * 1024ULL));
    doc["sd_total_mb"] = (uint32_t)(sd_total_bytes() / (1024ULL * 1024ULL));

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
    g["online"]     = grid.online;
    g["power_w"]    = grid.power_w;
    g["today_kwh"]  = grid.today_kwh;
    g["week_kwh"]   = grid.week_kwh;
    g["month_kwh"]  = grid.month_kwh;
    g["voltage_v"]  = grid.voltage_v;
    g["current_a"]  = grid.current_a;
    g["device"]     = grid_device_label(g_cfg.grid_device);
    g["host"]       = g_cfg.grid_host;

    // Aggregate solar total + per-source array
    uint32_t total_max_w = 0;
    int first_active = -1;
    for (int i = 0; i < MAX_SOLAR; i++) {
        if (g_cfg.solars[i].device == SolarDevice::NONE) continue;
        total_max_w += g_cfg.solars[i].max_w;
        if (first_active < 0) first_active = i;
    }
    auto s = doc["solar"].to<JsonObject>();
    s["online"]     = solar.online;
    s["power_w"]    = solar.power_w;
    s["today_kwh"]  = solar.today_kwh;
    s["week_kwh"]   = solar.week_kwh;
    s["month_kwh"]  = solar.month_kwh;
    s["voltage_v"]  = solar.voltage_v;
    s["current_a"]  = solar.current_a;
    s["max_w"]      = total_max_w;
    s["dc_voltage"] = (first_active >= 0) ? solar_src[first_active].dc_voltage : 0.0f;
    s["limit_pct"]  = (first_active >= 0) ? solar_src[first_active].limit_pct  : 0;
    s["is_opendtu"] = (first_active >= 0 &&
                       g_cfg.solars[first_active].device == SolarDevice::OPENDTU);
    auto srcArr = s["sources"].to<JsonArray>();
    for (int i = 0; i < MAX_SOLAR; i++) {
        if (g_cfg.solars[i].device == SolarDevice::NONE) continue;
        auto src = srcArr.add<JsonObject>();
        src["idx"]          = i;
        src["name"]         = g_cfg.solars[i].name;
        src["device"]       = solar_device_label(g_cfg.solars[i].device);
        src["host"]         = g_cfg.solars[i].host;
        src["max_w"]        = g_cfg.solars[i].max_w;
        src["online"]       = solar_src[i].online;
        src["power_w"]      = solar_src[i].power_w;
        src["today_kwh"]    = solar_src[i].today_kwh;
        src["week_kwh"]     = solar_src[i].week_kwh;
        src["month_kwh"]    = solar_src[i].month_kwh;
        src["voltage_v"]    = solar_src[i].voltage_v;
        src["current_a"]    = solar_src[i].current_a;
        src["dc_voltage"]   = solar_src[i].dc_voltage;
        src["limit_pct"]    = solar_src[i].limit_pct;
    }

    auto bats = doc["batteries"].to<JsonArray>();
    for (int i = 0; i < MAX_BATTERIES; i++) {
        if (g_cfg.batteries[i].device == BatteryDevice::NONE) continue;
        auto b = bats.add<JsonObject>();
        b["idx"]       = i;
        b["name"]      = g_cfg.batteries[i].name;
        b["online"]    = batteries[i].online;
        b["power_w"]   = batteries[i].power_w;
        b["voltage_v"] = batteries[i].voltage_v;
        b["current_a"] = batteries[i].current_a;
        b["soc_pct"]   = batteries[i].soc_pct;
        b["week_kwh"]  = batteries[i].week_kwh;
        b["month_kwh"] = batteries[i].month_kwh;
        b["device"]    = battery_device_label(g_cfg.batteries[i].device);
    }
    auto rtrs = doc["routers"].to<JsonArray>();
    bool f1atb_direct = (g_cfg.grid_device == GridDevice::F1ATB &&
                         g_cfg.routers[0].device == RouterDevice::NONE);
    if (f1atb_direct) {
        auto r = rtrs.add<JsonObject>();
        r["idx"]              = 0;
        r["name"]             = g_cfg.routers[0].name;
        r["online"]           = routers[0].online;
        r["power_w"]          = routers[0].power_w;
        r["voltage_v"]        = routers[0].voltage_v;
        r["current_a"]        = routers[0].current_a;
        r["triac_pct"]        = routers[0].triac_pct;
        r["today_kwh"]        = routers[0].today_kwh;
        r["week_kwh"]         = routers[0].week_kwh;
        r["month_kwh"]        = routers[0].month_kwh;
        r["duration_h"]       = routers[0].duration_h;
        r["week_duration_h"]  = routers[0].week_duration_h;
        r["month_duration_h"] = routers[0].month_duration_h;
        r["active"]           = routers[0].active;
        r["device"]           = "F1ATB direct";
    }
    for (int i = 0; i < MAX_ROUTERS; i++) {
        if (g_cfg.routers[i].device == RouterDevice::NONE) continue;
        auto r = rtrs.add<JsonObject>();
        r["idx"]              = i;
        r["name"]             = g_cfg.routers[i].name;
        r["online"]           = routers[i].online;
        r["power_w"]          = routers[i].power_w;
        r["voltage_v"]        = routers[i].voltage_v;
        r["current_a"]        = routers[i].current_a;
        r["triac_pct"]        = routers[i].triac_pct;
        r["today_kwh"]        = routers[i].today_kwh;
        r["week_kwh"]         = routers[i].week_kwh;
        r["month_kwh"]        = routers[i].month_kwh;
        r["duration_h"]       = routers[i].duration_h;
        r["week_duration_h"]  = routers[i].week_duration_h;
        r["month_duration_h"] = routers[i].month_duration_h;
        r["active"]           = routers[i].active;
        r["device"]           = router_device_label(g_cfg.routers[i].device);
    }

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

// ─── Données journalières ─────────────────────────────────────────────────────
static void handle_daily() {
    if (!s_day_snap || !s_web_buf) {
        server.send(503, "application/json", "{\"error\":\"buffer indisponible\"}");
        return;
    }
    int head, count;
    taskENTER_CRITICAL(&day_mux);
    memcpy(s_day_snap, day_ring, day_count * sizeof(DayPoint));
    head  = day_head;
    count = day_count;
    taskEXIT_CRITICAL(&day_mux);

    // Calcul autoconsommation / autosuffisance depuis les données instantanées
    double g_kwh = 0, s_kwh = 0, self_kwh = 0;
    const double dt_h = 300.0 / 3600.0;  // 5 min par point
    int start = (count < 288) ? 0 : head;
    for (int i = 0; i < count; i++) {
        int idx = (start + i) % 288;
        float gp = s_day_snap[idx].gw;
        float sp = s_day_snap[idx].sw;
        if (gp > 0) g_kwh  += gp * dt_h / 1000.0;
        if (sp > 0) s_kwh  += sp * dt_h / 1000.0;
        float self = sp + (gp < 0 ? gp : 0.0f);
        if (self > 0) self_kwh += self * dt_h / 1000.0;
    }
    int ac_pct  = (s_kwh  > 0.01) ? (int)min(100.0, self_kwh / s_kwh        * 100.0) : 0;
    int as_pct  = (g_kwh + self_kwh > 0.01) ? (int)min(100.0, self_kwh / (g_kwh + self_kwh) * 100.0) : 0;

    // Sérialisation (max 300 pts decimés) — s_web_buf partagé (22 KB PSRAM)
    const size_t BUF_SZ = 22000;
    size_t pos = 0;
    pos += snprintf(s_web_buf + pos, BUF_SZ - pos, "{\"pts\":[");
    int step = (count > 300) ? count / 300 : 1;
    bool first = true;
    for (int i = 0; i < count; i += step) {
        int idx = (start + i) % 288;
        if (!first) s_web_buf[pos++] = ',';
        first = false;
        int n = snprintf(s_web_buf + pos, BUF_SZ - pos,
                         "{\"ts\":%ld,\"gp\":%.0f,\"sp\":%.0f}",
                         (long)s_day_snap[idx].ts, s_day_snap[idx].gw, s_day_snap[idx].sw);
        pos += n;
        if (pos > BUF_SZ - 200) break;
    }
    snprintf(s_web_buf + pos, BUF_SZ - pos,
             "],\"g_kwh\":%.2f,\"s_kwh\":%.2f,\"ac\":%d,\"as_\":%d}",
             g_kwh, s_kwh, ac_pct, as_pct);
    server.send(200, "application/json", s_web_buf);
}

static void handle_config_get() {
    JsonDocument doc;
    doc["device_name"]          = g_cfg.device_name;
    doc["grid_name"]            = g_cfg.grid_name;
    doc["solar_name"]           = g_cfg.solar_name;
    doc["grid_device"]          = (int)g_cfg.grid_device;
    doc["grid_host"]            = g_cfg.grid_host;
    doc["display_rotation"]     = g_cfg.display_rotation;
    doc["ha_token"]             = g_cfg.ha_token;
    doc["grid_entity"]          = g_cfg.grid_entity;
    doc["grid_energy_entity"]   = g_cfg.grid_energy_entity;
    doc["grid_voltage_entity"]  = g_cfg.grid_voltage_entity;
    doc["grid_current_entity"]  = g_cfg.grid_current_entity;
    doc["timezone"]             = g_cfg.timezone;

    auto solArr = doc["solars"].to<JsonArray>();
    for (int i = 0; i < MAX_SOLAR; i++) {
        auto s = solArr.add<JsonObject>();
        s["device"]        = (int)g_cfg.solars[i].device;
        s["name"]          = g_cfg.solars[i].name;
        s["host"]          = g_cfg.solars[i].host;
        s["user"]          = g_cfg.solars[i].user;
        s["pass"]          = g_cfg.solars[i].pass;
        s["serial"]        = g_cfg.solars[i].serial;
        s["max_w"]         = g_cfg.solars[i].max_w;
        s["entity"]        = g_cfg.solars[i].entity;
        s["energy_entity"] = g_cfg.solars[i].energy_entity;
    }

    auto batArr = doc["batteries"].to<JsonArray>();
    for (int i = 0; i < MAX_BATTERIES; i++) {
        auto b = batArr.add<JsonObject>();
        b["device"]          = (int)g_cfg.batteries[i].device;
        b["name"]            = g_cfg.batteries[i].name;
        b["host"]            = g_cfg.batteries[i].host;
        b["power_entity"]    = g_cfg.batteries[i].power_entity;
        b["soc_entity"]      = g_cfg.batteries[i].soc_entity;
        b["voltage_entity"]  = g_cfg.batteries[i].voltage_entity;
        b["current_entity"]  = g_cfg.batteries[i].current_entity;
    }
    auto rtrArr = doc["routers"].to<JsonArray>();
    for (int i = 0; i < MAX_ROUTERS; i++) {
        auto r = rtrArr.add<JsonObject>();
        r["device"]           = (int)g_cfg.routers[i].device;
        r["name"]             = g_cfg.routers[i].name;
        r["host"]             = g_cfg.routers[i].host;
        r["power_entity"]     = g_cfg.routers[i].power_entity;
        r["energy_entity"]    = g_cfg.routers[i].energy_entity;
        r["active_entity"]    = g_cfg.routers[i].active_entity;
        r["duration_entity"]  = g_cfg.routers[i].duration_entity;
        r["triac_entity"]     = g_cfg.routers[i].triac_entity;
        r["voltage_entity"]   = g_cfg.routers[i].voltage_entity;
        r["current_entity"]   = g_cfg.routers[i].current_entity;
    }
    auto hostsArr = doc["hosts"].to<JsonArray>();
    for (int i = 0; i < MAX_HOSTS; i++) {
        if (!g_cfg.hosts[i].name[0] && !g_cfg.hosts[i].ip[0]) continue;
        auto h = hostsArr.add<JsonObject>();
        h["name"] = g_cfg.hosts[i].name;
        h["ip"]   = g_cfg.hosts[i].ip;
    }

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
    strncpy(cfg.device_name,  doc["device_name"]  | cfg.device_name,  sizeof(cfg.device_name)  - 1);
    strncpy(cfg.grid_name,    doc["grid_name"]    | cfg.grid_name,    sizeof(cfg.grid_name)    - 1);
    strncpy(cfg.solar_name,   doc["solar_name"]   | cfg.solar_name,   sizeof(cfg.solar_name)   - 1);
    cfg.grid_device      = (GridDevice)(int)(doc["grid_device"] | (int)cfg.grid_device);
    strncpy(cfg.grid_host,           doc["grid_host"]           | cfg.grid_host,           sizeof(cfg.grid_host)           - 1);
    strncpy(cfg.grid_entity,         doc["grid_entity"]         | cfg.grid_entity,         sizeof(cfg.grid_entity)         - 1);
    strncpy(cfg.grid_energy_entity,  doc["grid_energy_entity"]  | cfg.grid_energy_entity,  sizeof(cfg.grid_energy_entity)  - 1);
    strncpy(cfg.grid_voltage_entity, doc["grid_voltage_entity"] | cfg.grid_voltage_entity, sizeof(cfg.grid_voltage_entity) - 1);
    strncpy(cfg.grid_current_entity, doc["grid_current_entity"] | cfg.grid_current_entity, sizeof(cfg.grid_current_entity) - 1);
    cfg.display_rotation = (uint8_t)(int)(doc["display_rotation"] | (int)cfg.display_rotation);
    strncpy(cfg.ha_token,  doc["ha_token"]  | cfg.ha_token,  sizeof(cfg.ha_token)  - 1);
    strncpy(cfg.timezone,  doc["timezone"]  | cfg.timezone,  sizeof(cfg.timezone)  - 1);
    // demo_mode depuis le JSON si fourni ; sinon auto-désactivé dès qu'un vrai appareil est configuré
    if (doc["demo_mode"].is<bool>()) {
        cfg.demo_mode = doc["demo_mode"].as<bool>();
    } else {
        bool has_device = (cfg.grid_device != GridDevice::NONE);
        for (int i = 0; !has_device && i < MAX_SOLAR; i++)
            has_device = (cfg.solars[i].device != SolarDevice::NONE);
        for (int i = 0; !has_device && i < MAX_BATTERIES; i++)
            has_device = (cfg.batteries[i].device != BatteryDevice::NONE);
        for (int i = 0; !has_device && i < MAX_ROUTERS; i++)
            has_device = (cfg.routers[i].device != RouterDevice::NONE);
        if (has_device) cfg.demo_mode = false;
    }

    if (doc["solars"].is<JsonArray>()) {
        JsonArrayConst solArr = doc["solars"].as<JsonArrayConst>();
        for (int i = 0; i < MAX_SOLAR; i++) cfg.solars[i] = {};
        int n = (int)solArr.size(); if (n > MAX_SOLAR) n = MAX_SOLAR;
        for (int i = 0; i < n; i++) {
            JsonObjectConst s = solArr[i];
            cfg.solars[i].device = (SolarDevice)(int)(s["device"] | 0);
            strncpy(cfg.solars[i].name,          s["name"]          | "", 31);
            strncpy(cfg.solars[i].host,          s["host"]          | "", 63);
            strncpy(cfg.solars[i].user,          s["user"]          | "admin", 31);
            strncpy(cfg.solars[i].pass,          s["pass"]          | "", 31);
            strncpy(cfg.solars[i].serial,        s["serial"]        | "", 63);
            cfg.solars[i].max_w = (uint16_t)(int)(s["max_w"] | 0);
            strncpy(cfg.solars[i].entity,        s["entity"]        | "", 63);
            strncpy(cfg.solars[i].energy_entity, s["energy_entity"] | "", 63);
        }
    }

    if (doc["batteries"].is<JsonArray>()) {
        JsonArrayConst batArr = doc["batteries"].as<JsonArrayConst>();
        for (int i = 0; i < MAX_BATTERIES; i++) cfg.batteries[i] = {};
        int n = (int)batArr.size(); if (n > MAX_BATTERIES) n = MAX_BATTERIES;
        for (int i = 0; i < n; i++) {
            JsonObjectConst b = batArr[i];
            cfg.batteries[i].device = (BatteryDevice)(int)(b["device"] | 0);
            strncpy(cfg.batteries[i].name,           b["name"]           | "", 31);
            strncpy(cfg.batteries[i].host,           b["host"]           | "", 63);
            strncpy(cfg.batteries[i].power_entity,   b["power_entity"]   | "", 63);
            strncpy(cfg.batteries[i].soc_entity,     b["soc_entity"]     | "", 63);
            strncpy(cfg.batteries[i].voltage_entity, b["voltage_entity"] | "", 63);
            strncpy(cfg.batteries[i].current_entity, b["current_entity"] | "", 63);
        }
    }
    if (doc["routers"].is<JsonArray>()) {
        JsonArrayConst rtrArr = doc["routers"].as<JsonArrayConst>();
        for (int i = 0; i < MAX_ROUTERS; i++) cfg.routers[i] = {};
        int n = (int)rtrArr.size(); if (n > MAX_ROUTERS) n = MAX_ROUTERS;
        for (int i = 0; i < n; i++) {
            JsonObjectConst r = rtrArr[i];
            cfg.routers[i].device = (RouterDevice)(int)(r["device"] | 0);
            strncpy(cfg.routers[i].name,            r["name"]            | "", 31);
            strncpy(cfg.routers[i].host,            r["host"]            | "", 63);
            strncpy(cfg.routers[i].power_entity,    r["power_entity"]    | "", 63);
            strncpy(cfg.routers[i].energy_entity,   r["energy_entity"]   | "", 63);
            strncpy(cfg.routers[i].active_entity,   r["active_entity"]   | "", 63);
            strncpy(cfg.routers[i].duration_entity, r["duration_entity"] | "", 63);
            strncpy(cfg.routers[i].triac_entity,    r["triac_entity"]    | "", 63);
            strncpy(cfg.routers[i].voltage_entity,  r["voltage_entity"]  | "", 63);
            strncpy(cfg.routers[i].current_entity,  r["current_entity"]  | "", 63);
        }
    }

    if (doc["hosts"].is<JsonArray>()) {
        JsonArrayConst hostsArr = doc["hosts"].as<JsonArrayConst>();
        for (int i = 0; i < MAX_HOSTS; i++) cfg.hosts[i] = {};
        int n = (int)hostsArr.size(); if (n > MAX_HOSTS) n = MAX_HOSTS;
        for (int i = 0; i < n; i++) {
            JsonObjectConst h = hostsArr[i];
            strncpy(cfg.hosts[i].name, h["name"] | "", 31);
            strncpy(cfg.hosts[i].ip,   h["ip"]   | "", 63);
        }
    }

    device_config_save(cfg);
    g_cfg = cfg;

    server.send(200, "application/json", "{\"ok\":true}");
    delay(300);
    ESP.restart();
}

static void handle_hosts_post() {
    String body = server.arg("plain");
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        server.send(400, "application/json", "{\"error\":\"json invalide\"}");
        return;
    }
    if (doc["hosts"].is<JsonArray>()) {
        JsonArrayConst hostsArr = doc["hosts"].as<JsonArrayConst>();
        for (int i = 0; i < MAX_HOSTS; i++) g_cfg.hosts[i] = {};
        int n = (int)hostsArr.size(); if (n > MAX_HOSTS) n = MAX_HOSTS;
        for (int i = 0; i < n; i++) {
            JsonObjectConst h = hostsArr[i];
            strncpy(g_cfg.hosts[i].name, h["name"] | "", 31);
            strncpy(g_cfg.hosts[i].ip,   h["ip"]   | "", 63);
        }
        // Sauvegarde uniquement les hotes en NVS — pas de redemarrage
        Preferences p; p.begin("dev_cfg", false);
        char k[14];
        for (int i = 0; i < MAX_HOSTS; i++) {
            snprintf(k, sizeof(k), "h%d_name", i); p.putString(k, g_cfg.hosts[i].name);
            snprintf(k, sizeof(k), "h%d_ip",   i); p.putString(k, g_cfg.hosts[i].ip);
        }
        p.end();
    }
    server.send(200, "application/json", "{\"ok\":true}");
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
    if (!s_web_buf) { server.send(503, "application/json", "{\"error\":\"buffer indisponible\"}"); return; }
    if (sd_get_history(s_web_buf, 22000, n))
        server.send(200, "application/json", s_web_buf);
    else
        server.send(500, "application/json", "{\"error\":\"buffer insuffisant\"}");
}

static void handle_realtime() {
    if (!rt_ring || !s_web_buf) {
        server.send(503, "application/json", "[]");
        return;
    }
    RTPoint snap[90];
    int head, count;
    taskENTER_CRITICAL(&rt_mux);
    memcpy(snap, rt_ring, sizeof(RTPoint) * 90);
    head  = rt_head;
    count = rt_count;
    taskEXIT_CRITICAL(&rt_mux);

    if (count == 0) {
        server.send(200, "application/json", "[]");
        return;
    }

    const size_t BUF_SZ = 8192;
    size_t pos = 0;
    s_web_buf[pos++] = '[';
    int start = (count < 90) ? 0 : head;
    for (int i = 0; i < count; i++) {
        int idx = (start + i) % 90;
        char entry[80];
        int n = snprintf(entry, sizeof(entry),
                         "{\"ts\":%ld,\"gp\":%.1f,\"sp\":%.1f}",
                         (long)snap[idx].ts, snap[idx].gw, snap[idx].sw);
        if (pos + n + 2 >= BUF_SZ) break;
        memcpy(s_web_buf + pos, entry, n);
        pos += n;
        if (i < count - 1) s_web_buf[pos++] = ',';
    }
    s_web_buf[pos++] = ']';
    s_web_buf[pos]   = '\0';
    server.send(200, "application/json", s_web_buf);
}

static void handle_demo_toggle() {
    String body = server.arg("plain");
    JsonDocument doc;
    bool new_state = !g_cfg.demo_mode;
    if (!deserializeJson(doc, body) && doc["demo"].is<bool>()) {
        new_state = doc["demo"].as<bool>();
    }
    g_cfg.demo_mode = new_state;
    Preferences p;
    p.begin("dev_cfg", false);
    p.putBool("demo_mode", new_state);
    p.end();
    Serial.printf("[demo] mode %s\n", new_state ? "ACTIF" : "inactif");
    server.send(200, "application/json", new_state ? "{\"demo\":true}" : "{\"demo\":false}");
}

static void handle_navigate() {
    String body = server.arg("plain");
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        server.send(400, "application/json", "{\"error\":\"json invalide\"}");
        return;
    }
    int screen = doc["screen"] | 0;
    if (screen < 0 || screen > 4) {
        server.send(400, "application/json", "{\"error\":\"ecran invalide\"}");
        return;
    }
    ui_navigate_request(screen);
    server.send(200, "application/json", "{\"ok\":true}");
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
    server.on("/api/hosts",         HTTP_POST, handle_hosts_post);
    server.on("/api/wifi",          HTTP_POST, handle_wifi_config);
    server.on("/api/wifi/scan",     HTTP_POST, handle_wifi_scan_start);
    server.on("/api/wifi/networks", HTTP_GET,  handle_wifi_networks);
    server.on("/api/restart",       HTTP_POST, handle_restart);
    server.on("/api/navigate",      HTTP_POST, handle_navigate);
    server.on("/api/demo",          HTTP_POST, handle_demo_toggle);
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

static void* psram_or_heap(size_t sz) {
    void *p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = malloc(sz);
    return p;
}

void webserver_start() {
    rt_ring    = (RTPoint*) psram_or_heap(sizeof(RTPoint)  * 90);
    day_ring   = (DayPoint*)psram_or_heap(sizeof(DayPoint) * 288);
    s_log_snap = (DayPoint*)psram_or_heap(sizeof(DayPoint) * 288);
    s_rst_tmp  = (DayPoint*)psram_or_heap(sizeof(DayPoint) * 288);
    s_day_snap = (DayPoint*)psram_or_heap(sizeof(DayPoint) * 288);
    s_web_buf  = (char*)    psram_or_heap(22000);
    if (rt_ring)    memset(rt_ring,    0, sizeof(RTPoint)  * 90);
    if (day_ring)   memset(day_ring,   0, sizeof(DayPoint) * 288);
    if (s_log_snap) memset(s_log_snap, 0, sizeof(DayPoint) * 288);
    if (s_rst_tmp)  memset(s_rst_tmp,  0, sizeof(DayPoint) * 288);
    if (s_day_snap) memset(s_day_snap, 0, sizeof(DayPoint) * 288);
    Serial.printf("[web] PSRAM buffers: rt=%p day=%p snap=%p buf=%p\n",
                  rt_ring, day_ring, s_day_snap, s_web_buf);
    xTaskCreatePinnedToCore(web_task, "web", 12288, nullptr, 1, nullptr, 0);
}
