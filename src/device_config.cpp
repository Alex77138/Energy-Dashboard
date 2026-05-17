#include "device_config.h"
#include "config.h"
#include <Preferences.h>

DeviceConfig g_cfg;

static const char *NVS_NS = "dev_cfg";

void device_config_load() {
    Preferences p;
    p.begin(NVS_NS, true);
    String s;

    s = p.getString("name", "Dash Energy");
    strncpy(g_cfg.device_name, s.c_str(), sizeof(g_cfg.device_name) - 1);

    s = p.getString("grid_name", "R\xc3\xa9seau");
    strncpy(g_cfg.grid_name,  s.c_str(), sizeof(g_cfg.grid_name)  - 1);
    s = p.getString("sol_name", "Solaire");
    strncpy(g_cfg.solar_name, s.c_str(), sizeof(g_cfg.solar_name) - 1);

    g_cfg.grid_device = (GridDevice)p.getUChar("grid_dev", DEFAULT_GRID_DEVICE);
    s = p.getString("grid_host", DEFAULT_GRID_HOST);
    strncpy(g_cfg.grid_host, s.c_str(), sizeof(g_cfg.grid_host) - 1);

    g_cfg.solar_device = (SolarDevice)p.getUChar("solar_dev", 0);
    s = p.getString("solar_host", "");
    strncpy(g_cfg.solar_host, s.c_str(), sizeof(g_cfg.solar_host) - 1);

    s = p.getString("solar_user", "admin");
    strncpy(g_cfg.solar_user, s.c_str(), sizeof(g_cfg.solar_user) - 1);

    s = p.getString("solar_pass", "openDTU42");
    strncpy(g_cfg.solar_pass, s.c_str(), sizeof(g_cfg.solar_pass) - 1);

    s = p.getString("solar_serial", "");
    strncpy(g_cfg.solar_serial, s.c_str(), sizeof(g_cfg.solar_serial) - 1);

    g_cfg.solar_max_w       = p.getUShort("solar_max_w", 0);
    g_cfg.display_rotation  = p.getUChar("disp_rot", 0);

    s = p.getString("ha_token", "");
    strncpy(g_cfg.ha_token, s.c_str(), sizeof(g_cfg.ha_token) - 1);

    s = p.getString("grid_entity", DEFAULT_GRID_ENTITY);
    strncpy(g_cfg.grid_entity, s.c_str(), sizeof(g_cfg.grid_entity) - 1);

    s = p.getString("solar_entity", "");
    strncpy(g_cfg.solar_entity, s.c_str(), sizeof(g_cfg.solar_entity) - 1);

    s = p.getString("timezone", "CET-1CEST,M3.5.0,M10.5.0/3");
    strncpy(g_cfg.timezone, s.c_str(), sizeof(g_cfg.timezone) - 1);

    g_cfg.demo_mode = p.getBool("demo_mode", false);

    s = p.getString("grid_en_ent", "");
    strncpy(g_cfg.grid_energy_entity, s.c_str(), sizeof(g_cfg.grid_energy_entity) - 1);
    s = p.getString("sol_en_ent", "");
    strncpy(g_cfg.solar_energy_entity, s.c_str(), sizeof(g_cfg.solar_energy_entity) - 1);

    // ── Batteries (tableau) ──────────────────────────────────────────────────
    char k[14];
    for (int i = 0; i < MAX_BATTERIES; i++) {
        snprintf(k, sizeof(k), "b%d_dev", i);
        g_cfg.batteries[i].device = (BatteryDevice)p.getUChar(k, 0);
        snprintf(k, sizeof(k), "b%d_name", i);
        s = p.getString(k, ""); strncpy(g_cfg.batteries[i].name, s.c_str(), 31);
        snprintf(k, sizeof(k), "b%d_host", i);
        s = p.getString(k, ""); strncpy(g_cfg.batteries[i].host, s.c_str(), 63);
        snprintf(k, sizeof(k), "b%d_pwr", i);
        s = p.getString(k, ""); strncpy(g_cfg.batteries[i].power_entity, s.c_str(), 63);
        snprintf(k, sizeof(k), "b%d_soc", i);
        s = p.getString(k, ""); strncpy(g_cfg.batteries[i].soc_entity, s.c_str(), 63);
    }
    // Migration depuis anciens champs (slot 0)
    if (g_cfg.batteries[0].device == BatteryDevice::NONE) {
        BatteryDevice od = (BatteryDevice)p.getUChar("bat_dev", 0);
        if (od != BatteryDevice::NONE) {
            g_cfg.batteries[0].device = od;
            s = p.getString("bat_host", "");    strncpy(g_cfg.batteries[0].host, s.c_str(), 63);
            s = p.getString("bat_pwr_ent", ""); strncpy(g_cfg.batteries[0].power_entity, s.c_str(), 63);
            s = p.getString("bat_soc_ent", ""); strncpy(g_cfg.batteries[0].soc_entity, s.c_str(), 63);
        }
    }

    // ── Routeurs (tableau) ───────────────────────────────────────────────────
    for (int i = 0; i < MAX_ROUTERS; i++) {
        snprintf(k, sizeof(k), "r%d_dev", i);
        g_cfg.routers[i].device = (RouterDevice)p.getUChar(k, 0);
        snprintf(k, sizeof(k), "r%d_name", i);
        s = p.getString(k, ""); strncpy(g_cfg.routers[i].name, s.c_str(), 31);
        snprintf(k, sizeof(k), "r%d_host", i);
        s = p.getString(k, ""); strncpy(g_cfg.routers[i].host, s.c_str(), 63);
        snprintf(k, sizeof(k), "r%d_pwr", i);
        s = p.getString(k, ""); strncpy(g_cfg.routers[i].power_entity, s.c_str(), 63);
        snprintf(k, sizeof(k), "r%d_en", i);
        s = p.getString(k, ""); strncpy(g_cfg.routers[i].energy_entity, s.c_str(), 63);
        snprintf(k, sizeof(k), "r%d_act", i);
        s = p.getString(k, ""); strncpy(g_cfg.routers[i].active_entity, s.c_str(), 63);
        snprintf(k, sizeof(k), "r%d_dur", i);
        s = p.getString(k, ""); strncpy(g_cfg.routers[i].duration_entity, s.c_str(), 63);
        snprintf(k, sizeof(k), "r%d_tri", i);
        s = p.getString(k, ""); strncpy(g_cfg.routers[i].triac_entity, s.c_str(), 63);
    }
    // Migration depuis anciens champs routeur (slot 0)
    if (g_cfg.routers[0].device == RouterDevice::NONE) {
        RouterDevice od = (RouterDevice)p.getUChar("rtr_dev", 0);
        if (od != RouterDevice::NONE) {
            g_cfg.routers[0].device = od;
            s = p.getString("rtr_host", "");    strncpy(g_cfg.routers[0].host, s.c_str(), 63);
            s = p.getString("rtr_pwr_ent", ""); strncpy(g_cfg.routers[0].power_entity, s.c_str(), 63);
            s = p.getString("rtr_en_ent", "");  strncpy(g_cfg.routers[0].energy_entity, s.c_str(), 63);
        }
    }

    // ── Bibliothèque d'hôtes ────────────────────────────────────────────────────
    for (int i = 0; i < MAX_HOSTS; i++) {
        snprintf(k, sizeof(k), "h%d_name", i);
        s = p.getString(k, ""); strncpy(g_cfg.hosts[i].name, s.c_str(), 31);
        snprintf(k, sizeof(k), "h%d_ip", i);
        s = p.getString(k, ""); strncpy(g_cfg.hosts[i].ip, s.c_str(), 63);
    }

    p.end();
}

void device_config_save(const DeviceConfig &cfg) {
    Preferences p;
    p.begin(NVS_NS, false);
    p.putString("name",         cfg.device_name);
    p.putString("grid_name",    cfg.grid_name);
    p.putString("sol_name",     cfg.solar_name);
    p.putUChar ("grid_dev",     (uint8_t)cfg.grid_device);
    p.putString("grid_host",    cfg.grid_host);
    p.putUChar ("solar_dev",    (uint8_t)cfg.solar_device);
    p.putString("solar_host",   cfg.solar_host);
    p.putString("solar_user",   cfg.solar_user);
    p.putString("solar_pass",   cfg.solar_pass);
    p.putString("solar_serial", cfg.solar_serial);
    p.putUShort("solar_max_w",  cfg.solar_max_w);
    p.putUChar ("disp_rot",     cfg.display_rotation);
    p.putString("ha_token",     cfg.ha_token);
    p.putString("grid_entity",  cfg.grid_entity);
    p.putString("solar_entity", cfg.solar_entity);
    p.putString("timezone",     cfg.timezone);
    p.putBool  ("demo_mode",    cfg.demo_mode);
    p.putString("grid_en_ent",  cfg.grid_energy_entity);
    p.putString("sol_en_ent",   cfg.solar_energy_entity);

    char k[14];
    for (int i = 0; i < MAX_BATTERIES; i++) {
        snprintf(k, sizeof(k), "b%d_dev", i);  p.putUChar (k, (uint8_t)cfg.batteries[i].device);
        snprintf(k, sizeof(k), "b%d_name", i); p.putString(k, cfg.batteries[i].name);
        snprintf(k, sizeof(k), "b%d_host", i); p.putString(k, cfg.batteries[i].host);
        snprintf(k, sizeof(k), "b%d_pwr", i);  p.putString(k, cfg.batteries[i].power_entity);
        snprintf(k, sizeof(k), "b%d_soc", i);  p.putString(k, cfg.batteries[i].soc_entity);
    }
    for (int i = 0; i < MAX_ROUTERS; i++) {
        snprintf(k, sizeof(k), "r%d_dev", i);  p.putUChar (k, (uint8_t)cfg.routers[i].device);
        snprintf(k, sizeof(k), "r%d_name", i); p.putString(k, cfg.routers[i].name);
        snprintf(k, sizeof(k), "r%d_host", i); p.putString(k, cfg.routers[i].host);
        snprintf(k, sizeof(k), "r%d_pwr", i);  p.putString(k, cfg.routers[i].power_entity);
        snprintf(k, sizeof(k), "r%d_en", i);   p.putString(k, cfg.routers[i].energy_entity);
        snprintf(k, sizeof(k), "r%d_act", i);  p.putString(k, cfg.routers[i].active_entity);
        snprintf(k, sizeof(k), "r%d_dur", i);  p.putString(k, cfg.routers[i].duration_entity);
        snprintf(k, sizeof(k), "r%d_tri", i);  p.putString(k, cfg.routers[i].triac_entity);
    }
    for (int i = 0; i < MAX_HOSTS; i++) {
        snprintf(k, sizeof(k), "h%d_name", i); p.putString(k, cfg.hosts[i].name);
        snprintf(k, sizeof(k), "h%d_ip", i);   p.putString(k, cfg.hosts[i].ip);
    }
    p.end();
}

const char *grid_device_label(GridDevice d) {
    switch (d) {
        case GridDevice::SHELLY_EM_1P:      return "Shelly EM Gen1 - 1 sonde";
        case GridDevice::SHELLY_EM_2P:      return "Shelly EM Gen1 - 2 sondes";
        case GridDevice::SHELLY_3EM_1P:     return "Shelly 3EM - 1 phase";
        case GridDevice::SHELLY_3EM_2P:     return "Shelly 3EM - 2 phases";
        case GridDevice::SHELLY_3EM_3P:     return "Shelly 3EM - 3 phases";
        case GridDevice::SHELLY_PRO_EM:     return "Shelly Pro EM (Gen2/3)";
        case GridDevice::SHELLY_PRO_3EM:    return "Shelly Pro 3EM (Gen2/3)";
        case GridDevice::F1ATB:             return "Routeur F1ATB";
        case GridDevice::HOME_ASSISTANT:    return "Home Assistant";
        default:                             return "Non configure";
    }
}

const char *battery_device_label(BatteryDevice d) {
    switch (d) {
        case BatteryDevice::ESPHOME_JKBMS:  return "syssi/esphome-jk-bms (ESPHome)";
        case BatteryDevice::HOME_ASSISTANT:  return "Home Assistant";
        default:                             return "Non configure";
    }
}

const char *router_device_label(RouterDevice d) {
    switch (d) {
        case RouterDevice::HOME_ASSISTANT:  return "Home Assistant";
        default:                            return "Non configure";
    }
}

const char *solar_device_label(SolarDevice d) {
    switch (d) {
        case SolarDevice::OPENDTU:          return "OpenDTU (Hoymiles)";
        case SolarDevice::AHOYDTU:          return "AhoyDTU (Hoymiles)";
        case SolarDevice::SHELLY_PLUG_G1:   return "Shelly Plug Gen1";
        case SolarDevice::SHELLY_PLUG_G2G3: return "Shelly Plug / Plus 1PM Gen2/3";
        case SolarDevice::SHELLY_EM_1P:     return "Shelly EM Gen1 - 1 sonde";
        case SolarDevice::SHELLY_EM_2P:     return "Shelly EM Gen1 - 2 sondes";
        case SolarDevice::SHELLY_3EM_1P:    return "Shelly 3EM - 1 phase";
        case SolarDevice::SHELLY_3EM_2P:    return "Shelly 3EM - 2 phases";
        case SolarDevice::SHELLY_3EM_3P:    return "Shelly 3EM - 3 phases";
        case SolarDevice::HOME_ASSISTANT:   return "Home Assistant";
        case SolarDevice::FRONIUS:          return "Onduleur Fronius";
        default:                             return "Non configure";
    }
}
