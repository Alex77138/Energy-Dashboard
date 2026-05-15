#pragma once
#include "types.h"

// API REST Fronius Symo/Primo/Galvo : /solar_api/v1/GetPowerFlowRealtimeData.fcgi
// P_PV > 0 = production solaire en cours
void fronius_fetch(SolarData &out, const char *host);
