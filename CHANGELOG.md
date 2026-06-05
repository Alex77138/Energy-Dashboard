# Changelog — Dash Energy

All notable changes to this project are documented here.
Format: [Version] — Description — Date

---

## [V1.2] — Persistance journalière + onglet Graphique + correctifs — 2026-06-05

### Corrections

- **Données journalières (`today_kwh`) remises à zéro au redémarrage**
  - Cause : `sd_save_daily` n'était appelé qu'une fois par jour (au changement
    de jour à minuit). Si l'ESP redémarrait avant minuit, le fichier conservait
    l'ancien `yday` → la restauration échouait → `grid_base = 0`.
  - Correction : sauvegarde périodique toutes les **5 minutes** (en plus de la
    sauvegarde au changement de jour). Au prochain redémarrage, la baseline est
    toujours fraîche (décalage max 5 min).

- **Double écriture SD à minuit**
  - Le bloc de changement de jour appelait `sd_save_daily`, puis la sauvegarde
    périodique le rappelait immédiatement dans le même cycle de poll.
  - Correction : `last_daily_save_ts` mis à jour dans le bloc de changement de
    jour pour neutraliser la sauvegarde périodique dans la même passe.

- **Double chargement de Chart.js sur clics rapides**
  - Des clics rapides sur l'onglet Graphique ajoutaient plusieurs balises
    `<script>` Chart.js simultanément.
  - Correction : drapeau `chartJsLoading` + file d'attente `chartJsCbs[]` —
    les callbacks s'accumulent et sont tous exécutés à la fin du chargement
    unique.

- **Protection NaN dans le calcul `today_kwh`**
  - `fmaxf(0.0f, NaN)` est un comportement indéfini sur certaines
    implémentations C.
  - Correction : remplacement par `(!isnan(g_d) && g_d > 0.0f) ? g_d : 0.0f`
    pour grille, solaire et routeur.

- **Vérification des retours `write()` / `read()` dans `sd_logger`**
  - `sd_save_daily` ignorait les valeurs de retour de `write()` et renvoyait
    toujours `true`, même en cas d'écriture partielle.
  - `sd_load_daily` ne vérifiait pas les lectures des tableaux solaire/routeur.
  - Correction : chaque appel `write()` / `read()` est désormais vérifié ; la
    fonction retourne `false` dès la première erreur.

### Ajouts

- **Configuration WiFi depuis l'écran tactile**
  - Au premier démarrage (aucun identifiant Wi-Fi enregistré en NVS), l'écran
    affiche automatiquement une interface de sélection réseau avec scan Wi-Fi
    et clavier tactile intégré.
  - Sélection du réseau par pression, saisie du mot de passe, connexion —
    tout depuis l'écran, sans ordinateur.
  - Les identifiants sont sauvegardés en NVS ; les démarrages suivants se
    reconnectent automatiquement sans repasser par cet écran.
  - L'AP captif (`DashEnergy-Config`) reste actif pendant la configuration
    pour permettre une connexion simultanée via navigateur si besoin.

- **Onglet Graphique dans l'interface web**
  - Deux courbes Chart.js 4.4 :
    - **Courbe 2 h** : ring buffer haute fréquence (dernières 2 heures)
    - **Courbe 24 h** : ring buffer journalier 288 pts (00:00 → maintenant)
  - Chargement différé de Chart.js depuis CDN — ne charge la bibliothèque que
    si l'onglet est ouvert.
  - Rafraîchissement automatique toutes les 5 min tant que l'onglet est actif ;
    arrêt automatique en quittant l'onglet.
  - Les indicateurs « Aujourd'hui réseau » et « Aujourd'hui solaire » déplacés
    dans cet onglet.

### Nettoyage

- Suppression des variables JS mortes `dayTid` et `chDay`.
- `startRefresh()` ne tente plus d'appeler `fetchDaily()` (supprimée).

### Technique

- `main.cpp` : ajout de `last_daily_save_ts` + bloc de sauvegarde périodique
  dans `poll_task`
- `sd_logger.cpp` : vérification exhaustive des retours d'E/S SD
- `webserver.cpp` : refonte onglet Graphique (`loadChartJs`, `fetchCharts`,
  `renderChart`, `showTab`)

---

## [V1.1] — Correctif : données routeur remises à zéro au redémarrage — 2026-06-04

### Corrections

- **Données routeur (`today_kwh`) remises à zéro à chaque redémarrage**
  - Cause : la baseline quotidienne des routeurs (`router_base`) n'était jamais
    sauvegardée sur la carte SD. Au boot, le premier poll la fixait à la valeur
    courante, ce qui ramenait `today_kwh` à 0.
  - Correction : extension du fichier `daily2.bin` (nouveau magic `0xDB`) pour
    inclure `n_router + router_base[]` après les données solaires existantes.
  - Au démarrage, `router_base` est restauré depuis la SD et `router_base_set`
    est positionné à `true` immédiatement, ce qui empêche le bloc de
    réinitialisation de l'écraser.
  - Rétrocompatible avec l'ancien format `0xDA` (router_base = 0 à la
    première lecture après mise à jour, puis correctement sauvegardé dès le
    premier changement de jour).

### Technique

- `sd_logger.cpp / sd_logger.h` : nouvelles signatures `sd_save_daily` et
  `sd_load_daily` avec paramètres `router_base[]` et `n_router`
- `main.cpp` : `sd_load_daily` restaure `router_base` et arme `router_base_set`
  dès la restauration des baselines journalières

---

## [V1.0] — Tactile, navigation 5 écrans, multi-sources solaires, hebdo/mensuel — 2026-05-18

### Ajouts

- Navigation tactile GT911 via TouchLib (mmMicky) — GT911 détecté par probe I2C + reset matériel RST
- 5 écrans LVGL : Principal · Solaire · Réseau · Batterie · Routeur
- Retour automatique à l'écran principal après 30 s d'inactivité
- Navigation depuis l'interface web (POST `/api/navigate`) et par tactile sur les cartes de l'écran principal
- Multi-sources solaires (jusqu'à 4) : Fronius, OpenDTU, AhoyDTU, Shelly, Home Assistant
- Tension (V) et intensité (A) pour réseau, batterie, routeur
- Énergie hebdomadaire et mensuelle persistée sur carte SD (`/period.bin`)
- Courbe journalière réseau/solaire sur l'interface web (ring buffer 288 pts)
- Ring buffer journalier restauré au redémarrage depuis SD
- Page de configuration : section solaire multi-sources avec bouton « + Ajouter »
- Onglet « Aide » dans l'interface web (mode d'emploi intégré)

### Corrections

- Coordonnées tactiles inversées à rotation 180° : suppression de l'inversion manuelle (LVGL sw_rotate gère la transformation)
- `today_kwh` batteries absent en mode démo
- Affichage `"Auj: -- kWh"` figé sur l'écran de détail batterie
- Accumulation hebdomadaire/mensuelle batterie ne comptabilisait pas la journée en cours
- **Mode démo persistant** : `device_config_load()` désactive et efface automatiquement `demo_mode` en NVS au boot si au moins un vrai appareil est configuré ; `handle_config_post()` désactive également le mode démo à la sauvegarde dès qu'un appareil est présent

### Sécurité

- Credentials WiFi retirés de `config.h` (remplacés par placeholders)
- `config.h` ajouté à `.gitignore` ; un fichier `config.h.example` est fourni

---

## [V0.9] — UI/Web améliorations + correctifs énergie journalière

### Ajouts

- Page de statut : indicateur autoconsommation / autosuffisance
- Graphiques temps réel réseau + solaire (ring buffer haute fréquence 90 pts × 4 s)
- Courbe journalière : histogramme 5 min (288 pts max)
- Navigation web entre les 5 écrans LVGL (boutons dans l'onglet Statut)
- Bouton « ← Retour » sur chaque page de détail

### Corrections

- Énergie journalière : correction cumul → delta (baseline sauvée sur SD au changement de jour)
- Totalisation solaire multi-sources recalculée après correction journalière
- `router_base` initialisé dès le premier poll (évite énergie routeur négative)

---

## [V0.8] — Refonte config, corrections écran, mode démo

### Ajouts

- Mode démo : données simulées animées (solaire, réseau, batteries, routeurs)
- Interface web refactorisée : config multi-appareils avec accordéons
- Support Fronius Symo/Primo via API Solar.web locale
- Tension/intensité sur les cartes réseau, batterie, routeur
- MQTT : publication Home Assistant Discovery + topics personnalisables

### Corrections

- Jitter écran au démarrage : `bounce_buffer_size_px=0` dans Arduino_GFX
- Crash IDF 5.x : `esp_cache_msync` remplace `Cache_WriteBack_Addr` dépréciée
- Jauge solaire : correction de la normalisation (basée sur `max_w` configuré)

---

## [V0.7] — Intégration JK-BMS + fix jauge arc écran

### Ajouts

- Batterie JK-BMS via ESPHome `web_server` (syssi/esphome-jk-bms)
- Batterie via Home Assistant (entités puissance + SoC)
- Jusqu'à 4 batteries configurables
- Jauge SOC en arc LVGL sur l'écran de détail batterie

### Corrections

- Arc LVGL : `lv_arc_set_range` avant `lv_arc_set_value` pour éviter le clamp silencieux
- Rotation 180° : `LV_DISP_ROT_180` + `sw_rotate=1` — suppression de la séquence couleurs au démarrage

---

## [V0.6] — État de référence (diagnostic écran blanc)

Migration de LovyanGFX vers Arduino_GFX v1.2.9 — résolution du problème d'écran blanc
(LovyanGFX : GDMA cassé sur ESP-IDF 5.x pour le panneau RGB JC8048W550)

### Inclus à ce stade

- Affichage RGB 800×480, pilote GT911 (I2C manuel), LVGL 8.4
- Un écran principal : réseau, solaire (1 source), batteries, routeurs
- Interface web : statut, configuration, OTA
- Persistance journalière sur SD
- Support OpenDTU, AhoyDTU, Shelly EM/3EM/Pro, Home Assistant, F1ATB

---

## [V0.5 et antérieures]

Développement initial non tagué — voir `git log` pour le détail des commits.

Fonctionnalités progressivement intégrées :

- Portage LovyanGFX (abandonné pour cause d'écran blanc)
- Premier affichage LVGL fonctionnel
- Intégration Shelly EM, 3EM, Shelly Pro
- Routeurs F1ATB (2 instances)
- Persistance journalière sur SD card
- Jauge `lv_bar` énergie + correction données
- Configuration NVS complète
- Rotation 180° via l'interface web
