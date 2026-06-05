// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Alexandre Richard -- https://github.com/Alex77138/Energy-Dashboard
#pragma once
#include "types.h"

// Réseau : écriture dans MeasureData (compatible GridData et SolarData par héritage)
bool shelly_em_fetch    (MeasureData &out, const char *host, int probes);   // Gen1, 1-2 sondes
bool shelly_3em_fetch   (MeasureData &out, const char *host, int probes);   // Gen1, 1-3 phases
bool shelly_pro_em_fetch(MeasureData &out, const char *host, int channels); // Gen2/3, 2-3 canaux

// Solaire : Shelly Plug
bool shelly_plug_g1_fetch   (MeasureData &out, const char *host); // Gen1 (/status)
bool shelly_plug_g2g3_fetch (MeasureData &out, const char *host); // Gen2/3 (RPC)
