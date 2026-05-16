#pragma once
#include "types.h"

// syssi/esphome-jk-bms via ESPHome web_server HTTP
bool esphome_jkbms_fetch(BatteryData &out, const char *host);
