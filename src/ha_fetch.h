#pragma once
#include "types.h"

// Récupère l'état d'une entité Home Assistant via l'API REST.
// host = "192.168.1.x:8123" (avec port si différent de 80)
// token = Long-Lived Access Token HA
// entity = entity_id (ex: "sensor.linky_power")
void ha_fetch_grid(GridData &out, const char *host, const char *token, const char *entity);
void ha_fetch_solar(SolarData &out, const char *host, const char *token, const char *entity);
