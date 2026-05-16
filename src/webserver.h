#pragma once
#include "types.h"
void webserver_start();
void webserver_log(const AppData &d);        // appeler depuis poll_task pour les graphiques
void webserver_restore_day_ring();           // restaurer le ring depuis la SD (après NTP sync)
