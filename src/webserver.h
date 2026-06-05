// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Alexandre Richard -- https://github.com/Alex77138/Energy-Dashboard
#pragma once
#include "types.h"
void webserver_start();
void webserver_log(const AppData &d);        // appeler depuis poll_task pour les graphiques
void webserver_restore_day_ring();           // restaurer le ring depuis la SD (après NTP sync)
