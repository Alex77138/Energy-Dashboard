#pragma once
#include <Arduino.h>

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
    ESPHOME_JKBMS,  // syssi/esphome-jk-bms via ESPHome web_server = 1
};

struct DeviceConfig {
    char     device_name[32]   = "Dash Energy";
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
    // Batterie
    BatteryDevice battery_device = BatteryDevice::NONE;
    char     battery_host[64]    = "";
    // Heure
    char     timezone[64]        = "CET-1CEST,M3.5.0,M10.5.0/3";  // France par défaut
};

extern DeviceConfig g_cfg;

void device_config_load();
void device_config_save(const DeviceConfig &cfg);

const char *grid_device_label(GridDevice d);
const char *solar_device_label(SolarDevice d);
const char *battery_device_label(BatteryDevice d);
