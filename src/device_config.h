#pragma once
#include <Arduino.h>

#define MAX_BATTERIES 4
#define MAX_ROUTERS   4
#define MAX_HOSTS     8

enum class GridDevice : uint8_t {
    NONE = 0,
    SHELLY_EM_1P,
    SHELLY_EM_2P,
    SHELLY_3EM_1P,
    SHELLY_3EM_2P,
    SHELLY_3EM_3P,
    SHELLY_PRO_EM,
    SHELLY_PRO_3EM,
    F1ATB,
    HOME_ASSISTANT,  // = 9
};

enum class SolarDevice : uint8_t {
    NONE = 0,
    OPENDTU,
    AHOYDTU,
    SHELLY_PLUG_G1,
    SHELLY_PLUG_G2G3,
    SHELLY_EM_1P,
    SHELLY_EM_2P,
    SHELLY_3EM_1P,
    SHELLY_3EM_2P,
    SHELLY_3EM_3P,
    HOME_ASSISTANT,  // = 10
    FRONIUS,         // = 11
};

enum class BatteryDevice : uint8_t {
    NONE = 0,
    ESPHOME_JKBMS,   // syssi/esphome-jk-bms via ESPHome web_server = 1
    HOME_ASSISTANT,  // deux entités HA (puissance + SoC) = 2
};

enum class RouterDevice : uint8_t {
    NONE = 0,
    HOME_ASSISTANT,  // entités HA = 1
};

struct HostEntry {
    char name[32] = "";
    char ip[64]   = "";
};

struct BatteryConfig {
    BatteryDevice device         = BatteryDevice::NONE;
    char name[32]                = "";  // Nom affiché (ex: "Batterie salon")
    char host[64]                = "";
    char power_entity[64]        = "";  // HA : puissance (W)
    char soc_entity[64]          = "";  // HA : SoC (%)
};

struct RouterConfig {
    RouterDevice device          = RouterDevice::NONE;
    char name[32]                = "";  // Nom affiché (ex: "Chauffe-eau cuisine")
    char host[64]                = "";
    char power_entity[64]        = "";  // HA : puissance routée (W)
    char energy_entity[64]       = "";  // HA : énergie routée aujourd'hui (kWh) — optionnel
    char active_entity[64]       = "";  // HA : actif/inactif (binary_sensor)
    char duration_entity[64]     = "";  // HA : durée équivalente (h décimales)
    char triac_entity[64]        = "";  // HA : % ouverture triac (0–100)
};

struct DeviceConfig {
    char     device_name[32]   = "Dash Energy";
    // Noms affichés (personnalisables)
    char     grid_name[32]     = "R\xc3\xa9seau";   // UTF-8: "Réseau"
    char     solar_name[32]    = "Solaire";
    GridDevice  grid_device    = GridDevice::NONE;
    char     grid_host[64]     = "";
    SolarDevice solar_device   = SolarDevice::NONE;
    char     solar_host[64]    = "";
    char     solar_user[32]    = "admin";
    char     solar_pass[32]    = "openDTU42";
    char     solar_serial[64]  = "";    // "SN1/SN2/SN3" ou "ID1/ID2"
    uint16_t solar_max_w       = 0;    // Wc crête, 0 = jauge désactivée
    uint8_t  display_rotation  = 0;   // 0 = normal, 2 = 180°
    // Home Assistant REST API
    char     ha_token[192]     = "";   // Bearer token
    char     grid_entity[64]   = "";   // entity_id puissance réseau (W)
    char     solar_entity[64]  = "";   // entity_id puissance solaire (W)
    // HA : entités énergie journalière (optionnel — kWh déjà quotidien, bypass baseline)
    char     grid_energy_entity[64]    = "";
    char     solar_energy_entity[64]   = "";
    // Batteries (jusqu'à MAX_BATTERIES sources indépendantes)
    BatteryConfig batteries[MAX_BATTERIES];
    // Routeurs solaires (jusqu'à MAX_ROUTERS sources indépendantes)
    RouterConfig  routers[MAX_ROUTERS];
    // Bibliothèque d'hôtes (adresses IP réutilisables)
    HostEntry hosts[MAX_HOSTS];
    // Heure
    char     timezone[64]        = "CET-1CEST,M3.5.0,M10.5.0/3";  // France par défaut
    // Mode démo
    bool     demo_mode           = false;
};

extern DeviceConfig g_cfg;

void device_config_load();
void device_config_save(const DeviceConfig &cfg);

const char *grid_device_label(GridDevice d);
const char *solar_device_label(SolarDevice d);
const char *battery_device_label(BatteryDevice d);
const char *router_device_label(RouterDevice d);
