#pragma once
#include "types.h"
#include "device_config.h"

// Récupère l'état d'une entité Home Assistant via l'API REST.
// host = "192.168.1.x:8123" (avec port si différent de 80)
// token = Long-Lived Access Token HA
// entity = entity_id (ex: "sensor.linky_power")
void  ha_fetch_grid(GridData &out, const char *host, const char *token, const char *entity);
void  ha_fetch_solar(SolarData &out, const char *host, const char *token, const char *entity);
// Retourne la valeur de l'entité (kWh journalier) ou NAN si indisponible
float ha_fetch_energy(const char *host, const char *token, const char *entity);
// Batterie via deux entités HA (puissance W + SoC %)
void  ha_fetch_battery(BatteryData &out, const char *host, const char *token,
                       const char *power_entity, const char *soc_entity);
// Routeur solaire via entités HA
void  ha_fetch_router(RouterData &out, const char *host, const char *token,
                      const RouterConfig &cfg);
