// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Alexandre Richard -- https://github.com/Alex77138/Energy-Dashboard
#pragma once
#include "types.h"

bool opendtu_fetch    (SolarData &out,
                       const char *host, const char *user,
                       const char *pass, const char *serial);

bool opendtu_set_power(const char *host, const char *user,
                       const char *pass, const char *serial, bool enabled);

bool opendtu_set_limit(const char *host, const char *user,
                       const char *pass, const char *serial, int percent);

// AhoyDTU (Hoymiles) — serial = ID numérique de l'onduleur, "0" par défaut
bool ahoydtu_fetch(SolarData &out, const char *host, const char *serial);
