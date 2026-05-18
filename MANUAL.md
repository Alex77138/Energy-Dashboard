# Dash Energy — Mode d'emploi

**Version firmware : V1.0**

---

## Table des matières

1. [Présentation](#1-présentation)
2. [Matériel requis](#2-matériel-requis)
3. [Installation du firmware](#3-installation-du-firmware)
4. [Premier démarrage](#4-premier-démarrage)
5. [Interface web — Statut](#5-interface-web--statut)
6. [Interface web — Configuration](#6-interface-web--configuration)
   - 6.1 [Mode démo](#61-mode-démo)
   - 6.2 [Général](#62-général)
   - 6.3 [Réseau](#63-réseau)
   - 6.4 [Solaire](#64-solaire-multi-sources)
   - 6.5 [Batteries](#65-batteries)
   - 6.6 [Routeurs solaires](#66-routeurs-solaires)
   - 6.7 [MQTT](#67-mqtt)
   - 6.8 [Wi-Fi](#68-wi-fi)
   - 6.9 [Home Assistant — Token](#69-home-assistant--token)
7. [Interface web — Mise à jour OTA](#7-interface-web--mise-à-jour-ota)
8. [Écran principal](#8-écran-principal)
9. [Pages de détail](#9-pages-de-détail)
10. [Navigation entre écrans](#10-navigation-entre-écrans)
11. [Carte SD](#11-carte-sd)
12. [MQTT & Home Assistant](#12-mqtt--home-assistant)
13. [Dépannage](#13-dépannage)

---

## 1. Présentation

**Dash Energy** est un tableau de bord énergie en temps réel conçu pour surveiller votre installation solaire, votre consommation réseau, vos batteries et vos routeurs solaires sur un écran tactile 800×480.

Il s'installe sur la carte **Guition JC8048W550** (ESP32-S3, 5 pouces) et se configure entièrement depuis un navigateur web, sans outil supplémentaire.

**Ce que vous pouvez surveiller :**
- Production solaire (jusqu'à 4 sources indépendantes)
- Puissance réseau (import / export)
- État des batteries (jusqu'à 4)
- Routeurs solaires / chauffe-eau (jusqu'à 4)
- Énergie du jour, de la semaine et du mois pour chaque source

---

## 2. Matériel requis

| Élément | Requis | Notes |
|---|---|---|
| Guition JC8048W550 | ✅ Obligatoire | Disponible sur AliExpress |
| Câble USB-C | ✅ | Pour le flash initial |
| Alimentation 5 V / 2 A | ✅ | Via USB-C |
| Carte microSD FAT32 | ⚡ Recommandée | Persistance hebdo/mensuel |
| Réseau Wi-Fi 2.4 GHz | ✅ | L'ESP32-S3 ne supporte pas le 5 GHz |

---

## 3. Installation du firmware

### Option A — PlatformIO (depuis les sources)

1. Installez [VS Code](https://code.visualstudio.com/) + l'extension PlatformIO
2. Clonez le dépôt :
   ```
   git clone https://github.com/Alex77138/Energy-Dashboard.git
   ```
3. Copiez le fichier de configuration :
   ```
   cp src/config.h.example src/config.h
   ```
4. Éditez `src/config.h` et renseignez votre SSID/mot de passe Wi-Fi
5. Branchez la carte en USB et cliquez sur **Upload** dans PlatformIO
6. Ouvrez le moniteur série à **115200 baud** pour suivre le démarrage

### Option B — Fichier .bin précompilé (bêta testeurs)

1. Téléchargez le fichier `firmware_v1.0.bin` depuis la page [Releases](https://github.com/Alex77138/Energy-Dashboard/releases)
2. Utilisez [ESP Web Flasher](https://espressif.github.io/esptool-js/) **ou** `esptool.py` :
   ```
   esptool.py --port /dev/ttyUSB0 --baud 460800 \
     --before default_reset --after hard_reset \
     write_flash 0x0 firmware_v1.0.bin
   ```
3. Au premier démarrage, configurez le Wi-Fi via le portail captif (voir section 4)

> **Note :** Maintenez le bouton **BOOT** (GPIO 0) enfoncé pendant le branchement si l'ESP ne rentre pas en mode flash automatiquement.

---

## 4. Premier démarrage

À la mise sous tension :

1. L'écran s'allume — le logo et la version s'affichent brièvement dans le moniteur série
2. L'écran principal apparaît (vide ou en mode démo si aucun appareil n'est configuré)
3. Un point d'accès Wi-Fi **DashEnergy-Config** est créé (sans mot de passe)

**Configuration Wi-Fi initiale :**

1. Connectez votre téléphone ou ordinateur au réseau **DashEnergy-Config**
2. Ouvrez un navigateur et allez sur `http://192.168.4.1/`
3. Allez dans l'onglet **Configuration** → section **Wi-Fi**
4. Entrez votre SSID et mot de passe, puis cliquez **Sauvegarder le Wi-Fi**
5. Le tableau de bord redémarre et se connecte à votre réseau

**Après connexion :**
- L'adresse IP locale est affichée dans le moniteur série : `[wifi]  OK — 192.168.x.x`
- Accédez à l'interface via `http://192.168.x.x/` ou `http://dashenergy.local/`
- Le point d'accès se désactive automatiquement après connexion

> Si le Wi-Fi est perdu, le point d'accès se réactive automatiquement et l'ESP retente la connexion toutes les 30 secondes.

---

## 5. Interface web — Statut

L'onglet **Statut** affiche en temps réel :

- **Indicateur global** (bandeau coloré) :
  - Vert : autoconsommation — vous produisez plus que vous ne consommez
  - Orange : production solaire présente mais insuffisante
  - Rouge : import réseau — vous achetez de l'électricité
  - Gris : hors ligne / pas de données

- **Cartes réseau et solaire** : puissance instantanée (W), énergie du jour/semaine/mois, tension, intensité

- **Batteries** : puissance (+= charge, -= décharge), SOC %, tension, intensité

- **Routeurs solaires** : puissance déviée (W), durée aujourd'hui/semaine/mois, énergie déviée

- **Graphiques** :
  - Courbe temps réel (6 dernières minutes, mise à jour en continu)
  - Courbe journalière (aujourd'hui, point toutes les 5 minutes)

- **Boutons de navigation** : changer l'écran actif de la tablette depuis le navigateur

---

## 6. Interface web — Configuration

### 6.1 Mode démo

Active des données simulées et animées — aucun appareil réel n'est interrogé.  
Utile pour présenter le tableau de bord ou vérifier l'affichage sans installation.

### 6.2 Général

| Champ | Description |
|---|---|
| Nom du tableau de bord | Affiché dans le titre de la page web |
| Nom du réseau | Libellé affiché sur l'écran (ex : « Réseau EDF ») |
| Nom solaire | Libellé affiché sur l'écran (ex : « Panneaux ») |
| Orientation écran | `0` = USB en bas · `2` = USB en haut (180°) |
| Fuseau horaire | Syntaxe POSIX — ex : `CET-1CEST,M3.5.0,M10.5.0/3` pour la France |

### 6.3 Réseau

Sélectionnez le type d'appareil mesurant votre consommation/production réseau :

| Type | Adresse à renseigner | Entité(s) HA |
|---|---|---|
| Shelly EM (1P / 2P) | IP de la Shelly | — |
| Shelly 3EM (1P / 2P / 3P) | IP de la Shelly | — |
| Shelly Pro EM / Pro 3EM | IP de la Shelly | — |
| F1ATB | IP du routeur | — |
| Home Assistant | `IP:port` du serveur HA | Entité puissance (W) |

Pour Home Assistant, les champs optionnels **Entité énergie**, **Entité tension** et **Entité intensité** permettent d'afficher ces valeurs sur la page de détail réseau.

> **Signe de la puissance réseau :**  
> - Valeur **positive** = import (vous achetez)  
> - Valeur **négative** = export (vous revendez)  
>
> Configurez votre entité HA en conséquence (ou utilisez une entité signée).

### 6.4 Solaire (multi-sources)

Jusqu'à **4 sources solaires** indépendantes. Cliquez **« + Ajouter une source »** pour en ajouter.

Pour chaque source :

| Champ | Description |
|---|---|
| Nom | Libellé affiché sur la page de détail solaire |
| Type d'appareil | OpenDTU, AhoyDTU, Fronius, Shelly, Home Assistant |
| Adresse IP | IP de l'onduleur / passerelle |
| Puissance max (W) | Utilisé pour le pourcentage de la barre sur l'écran principal |
| Numéro de série | OpenDTU/AhoyDTU uniquement — identifie l'onduleur dans la passerelle |
| Utilisateur / Mot de passe | OpenDTU uniquement (défaut : `admin` / `openDTU42`) |
| Entité puissance (HA) | `sensor.nom_entite_puissance` |
| Entité énergie (HA) | `sensor.nom_entite_energie_jour` |

**L'écran principal affiche le total de toutes les sources.** La page de détail solaire affiche chaque source séparément.

### 6.5 Batteries

Jusqu'à **4 batteries** indépendantes. Pour chaque batterie :

| Champ | Description |
|---|---|
| Nom | Libellé affiché |
| Type | ESPHome JK-BMS · Home Assistant |
| Adresse IP | IP du module ESPHome **ou** IP:port du serveur HA |
| Entité puissance | `sensor.batterie_puissance` (W, positif = charge) |
| Entité SOC | `sensor.batterie_soc` (%) |
| Entité tension | Optionnel |
| Entité intensité | Optionnel |

**Pour JK-BMS via ESPHome :** installez le composant [syssi/esphome-jk-bms](https://github.com/syssi/esphome-jk-bms) sur un ESP dédié et activez `web_server`. Renseignez l'IP de cet ESP.

### 6.6 Routeurs solaires

Jusqu'à **4 routeurs** (chauffe-eau, résistances, etc.). Pour chaque routeur :

| Champ | Description |
|---|---|
| Nom | Libellé affiché |
| Type | F1ATB · Home Assistant |
| Entité puissance | Puissance déviée (W) |
| Entité énergie | Énergie déviée du jour (kWh) — peut être nulle |
| Entité durée | Durée de fonctionnement du jour (h) |
| Entité triac % | Pourcentage de modulation (0–100) |
| Entité actif | Booléen — le routeur est-il en route ? |
| Entité tension | Optionnel |
| Entité intensité | Optionnel |

### 6.7 MQTT

| Champ | Description |
|---|---|
| Activé | Active la publication MQTT |
| Serveur | IP ou hostname du broker |
| Port | Défaut : `1883` |
| Utilisateur / Mot de passe | Optionnel |
| Topic de base | Préfixe des topics publiés — ex : `dashenergy` |
| Discovery HA | Active l'auto-découverte Home Assistant |

**Topics publiés** (avec topic de base `dashenergy`) :
- `dashenergy/grid/power_w`
- `dashenergy/solar/power_w`
- `dashenergy/battery/0/soc_pct`
- `dashenergy/router/0/power_w`
- … (un topic par mesure et par appareil)

### 6.8 Wi-Fi

- **SSID / Mot de passe** : réseau 2.4 GHz  
- **IP fixe** : décochez DHCP et renseignez IP, passerelle, masque, DNS  

> Après sauvegarde du Wi-Fi, l'ESP redémarre. Vous devrez vous reconnecter à la nouvelle adresse IP si elle a changé.

### 6.9 Home Assistant — Token

Créez un jeton d'accès longue durée dans HA :  
*Profil* → *Sécurité* → *Jetons d'accès longue durée* → **Créer un jeton**

Collez ce jeton dans le champ **Token HA** de la configuration. Il est utilisé pour tous les appareils de type « Home Assistant ».

---

## 7. Interface web — Mise à jour OTA

1. Compilez ou téléchargez la nouvelle version du firmware (fichier `.bin`)
2. Allez dans l'onglet **Mise à jour**
3. Sélectionnez le fichier `.bin` et cliquez **Mettre à jour**
4. Une barre de progression apparaît — ne coupez pas l'alimentation
5. L'ESP redémarre automatiquement sur le nouveau firmware

> Le fichier `.bin` se trouve dans `.pio/build/jc8048w550/firmware.bin` après compilation.

---

## 8. Écran principal

L'écran principal affiche en un coup d'œil l'état de toute l'installation.

```
┌──────────────────────────────────────────────────────────────────────┐
│  [carte réseau]  │  [carte solaire]                                  │
│  250 W import    │  1 850 W  / 12.3 kWh auj                          │
│──────────────────┴──────────────────────────────────────────────────│
│  [bat 1]   [bat 2]   [rtr 1]   [rtr 2]                              │
│  +180 W    -45 W     620 W     390 W                                │
└──────────────────────────────────────────────────────────────────────┘
```

**Chaque carte est tactile** : touchez une carte pour accéder à la page de détail correspondante.

**Barre solaire** : représente le total solaire par rapport à la puissance max configurée.

**Indicateur autoconsommation** : affiché en bas de la carte solaire.
- `Auto: 94%` = 94 % de la production est consommée localement

---

## 9. Pages de détail

Chaque page de détail est accessible en touchant la carte correspondante sur l'écran principal ou via les boutons de navigation de l'interface web.

### Page solaire
- **Total** : puissance + énergie Auj / Sem / Mois
- **Par source** : une carte par source configurée, avec puissance, énergie, tension CC (OpenDTU)

### Page réseau
- Puissance instantanée (import positif / export négatif)
- Tension (V), Intensité (A)
- Énergie Auj / Sem / Mois

### Page batterie
- Une carte par batterie configurée
- Puissance (+= charge, -= décharge), Tension (V), Intensité (A), SOC %
- Énergie Auj / Sem / Mois

### Page routeur
- Une carte par routeur configuré
- Puissance déviée (W), Tension (V), Intensité (A)
- Durée équivalente Auj / Sem / Mois
- Énergie déviée Auj / Sem / Mois

### Bouton retour
Un bouton **← Retour** en haut à gauche de chaque page de détail permet de revenir à l'écran principal.

---

## 10. Navigation entre écrans

| Méthode | Comment faire |
|---|---|
| Tactile — écran principal | Touchez une carte pour aller sur la page correspondante |
| Tactile — page de détail | Touchez **← Retour** pour revenir à l'écran principal |
| Interface web | Onglet **Statut** → boutons de navigation en bas |
| API | `POST /api/navigate` avec `{"screen": N}` (0=principal, 1=solaire, 2=réseau, 3=batterie, 4=routeur) |

**Retour automatique :** si une page de détail est active et qu'il n'y a aucune interaction pendant **30 secondes**, le tableau de bord revient automatiquement à l'écran principal.

---

## 11. Carte SD

La carte SD conserve les données entre les redémarrages. Sans carte SD, l'énergie hebdomadaire et mensuelle est perdue à chaque redémarrage.

**Format requis :** FAT32 (formatez avec votre système d'exploitation).

**Fichiers créés automatiquement** dans le répertoire racine :

| Fichier | Contenu | Taille |
|---|---|---|
| `daily.bin` | Baselines journalières (réseau + solaires) | < 1 KB |
| `period.bin` | Bases hebdo/mensuelles (tous appareils) | < 2 KB |
| `day_ring.bin` | Courbe journalière (288 pts × 5 min) | ~7 KB |
| `log.csv` | Historique CSV horodaté | Croît avec le temps |

> L'utilisation de la SD est affichée dans la barre d'état de l'interface web.

---

## 12. MQTT & Home Assistant

Quand MQTT est activé, le tableau de bord publie toutes ses mesures sur votre broker.

### Structure des topics

```
<base>/grid/power_w          — puissance réseau (W)
<base>/grid/today_kwh        — énergie réseau du jour
<base>/grid/week_kwh         — énergie réseau semaine
<base>/grid/month_kwh        — énergie réseau mois
<base>/grid/voltage_v        — tension réseau
<base>/grid/current_a        — intensité réseau
<base>/solar/power_w         — puissance solaire totale
<base>/solar/today_kwh       — énergie solaire du jour
<base>/battery/0/power_w     — puissance batterie 1
<base>/battery/0/soc_pct     — SOC batterie 1 (%)
<base>/router/0/power_w      — puissance routeur 1
<base>/router/0/today_kwh    — énergie routée aujourd'hui
```

### Intégration Home Assistant

Activez **Discovery HA** dans la configuration MQTT.  
Les entités apparaîtront automatiquement dans HA sous le nom de votre tableau de bord.

---

## 13. Dépannage

### L'écran reste blanc au démarrage
- Vérifiez que le firmware correct pour la carte JC8048W550 est flashé
- Le crash GDMA est résolu depuis V0.8 (`bounce_buffer_size_px=0` dans le driver RGB)
- Vérifiez la console série pour les messages d'erreur

### Le tactile ne répond pas
- Vérifiez que l'orientation configurée correspond à l'orientation physique de la carte
- Si les touches ne correspondent pas aux bonnes zones, changez l'orientation (0 ↔ 2) dans la configuration
- La détection GT911 est confirmée dans la console : `[touch] GT911 OK`

### Aucune donnée réseau / solaire
1. Vérifiez que la carte est connectée au Wi-Fi (LED ou console série)
2. Vérifiez l'adresse IP de votre appareil dans la configuration
3. Testez depuis un navigateur : `http://IP_DE_L_APPAREIL/status` ou équivalent
4. Pour Home Assistant : vérifiez que le token est valide et que l'entité existe
5. Activez le mode démo pour vérifier que l'affichage fonctionne correctement

### L'énergie journalière repart de zéro après un redémarrage
- Normal sans carte SD
- Avec carte SD : vérifiez que la carte est bien détectée (`sd_ready: true` dans `/api/status`)
- Formatez la carte en FAT32 si elle n'est pas reconnue

### Le Wi-Fi ne se connecte pas
- Vérifiez que c'est un réseau 2.4 GHz (le 5 GHz n'est pas supporté)
- Le point d'accès **DashEnergy-Config** se réactive si le Wi-Fi est perdu
- Vérifiez le mot de passe dans Configuration → Wi-Fi

### L'interface web ne répond plus
- L'ESP peut être surchargé si trop d'appareils répondent lentement
- Un redémarrage via `POST /api/restart` ou le bouton RESET physique résout généralement le problème

### Mises à jour OTA échouent
- Vérifiez que le fichier `.bin` correspond bien à la carte jc8048w550 (PlatformIO env)
- Assurez-vous que la connexion réseau est stable pendant le transfert
- La taille max du firmware pour OTA est ~3 MB (partition OTA de 3 MB dans le schéma 16 MB)
