#pragma once
#include <Arduino.h>

#define MAX_BATTERIES 4
#define MAX_ROUTERS   4
#define MAX_HOSTS     8
#define MAX_SOLAR     4

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

struct SolarConfig {
    SolarDevice device         = SolarDevice::NONE;
    char name[32]              = "";
    char host[64]              = "";
    char user[32]              = "admin";
    char pass[32]              = "openDTU42";
    char serial[64]            = "";
    uint16_t max_w             = 0;
    char entity[64]            = "";
    char energy_entity[64]     = "";
};

struct BatteryConfig {
    BatteryDevice device        = BatteryDevice::NONE;
    char name[32]               = "";
    char host[64]               = "";
    char power_entity[64]       = "";
    char soc_entity[64]         = "";
    char voltage_entity[64]     = "";
    char current_entity[64]     = "";
};

struct RouterConfig {
    RouterDevice device         = RouterDevice::NONE;
    char name[32]               = "";
    char host[64]               = "";
    char power_entity[64]       = "";
    char energy_entity[64]      = "";
    char active_entity[64]      = "";
    char duration_entity[64]    = "";
    char triac_entity[64]       = "";
    char voltage_entity[64]     = "";
    char current_entity[64]     = "";
};

struct DeviceConfig {
    char     device_name[32]          = "Dash Energy";
    char     grid_name[32]            = "R\xc3\xa9seau";  // UTF-8: "Réseau"
    char     solar_name[32]           = "Solaire";
    GridDevice  grid_device           = GridDevice::NONE;
    char     grid_host[64]            = "";
    char     grid_entity[64]          = "";
    char     grid_energy_entity[64]   = "";
    char     grid_voltage_entity[64]  = "";
    char     grid_current_entity[64]  = "";
    uint8_t  display_rotation         = 0;
    char     ha_token[192]            = "";
    SolarConfig solars[MAX_SOLAR];
    BatteryConfig batteries[MAX_BATTERIES];
    RouterConfig  routers[MAX_ROUTERS];
    HostEntry hosts[MAX_HOSTS];
    char     timezone[64]             = "CET-1CEST,M3.5.0,M10.5.0/3";
    bool     demo_mode                = false;
};

extern DeviceConfig g_cfg;

void device_config_load();
void device_config_save(const DeviceConfig &cfg);

const char *grid_device_label(GridDevice d);
const char *solar_device_label(SolarDevice d);
const char *battery_device_label(BatteryDevice d);
const char *router_device_label(RouterDevice d);
