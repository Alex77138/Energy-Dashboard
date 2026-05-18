# Changelog — Dash Energy

All notable changes to this project are documented here.  
Format: [Version] — Description — Date

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
- **Mode démo persistant après sauvegarde** : `saveCfg()` inclut désormais `demo_mode` dans le payload ; `handle_config_post()` le lit → l'état du toggle est correctement sauvegardé en NVS à chaque clic sur « Sauvegarder »

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
