// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Alexandre Richard -- https://github.com/Alex77138/Energy-Dashboard
#pragma once
#include "types.h"
#include "device_config.h"

void  ha_fetch_grid(GridData &out, const char *host, const char *token,
                    const char *entity,
                    const char *volt_ent = nullptr,
                    const char *cur_ent  = nullptr);

void  ha_fetch_solar(SolarData &out, const char *host, const char *token,
                     const char *entity);

float ha_fetch_energy(const char *host, const char *token, const char *entity);

float ha_fetch_value(const char *host, const char *token, const char *entity);

void  ha_fetch_battery(BatteryData &out, const char *token,
                       const BatteryConfig &bc);

void  ha_fetch_router(RouterData &out, const char *host, const char *token,
                      const RouterConfig &cfg);
