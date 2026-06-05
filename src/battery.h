// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Alexandre Richard -- https://github.com/Alex77138/Energy-Dashboard
#pragma once
#include "types.h"

// syssi/esphome-jk-bms via ESPHome web_server HTTP
bool esphome_jkbms_fetch(BatteryData &out, const char *host);
