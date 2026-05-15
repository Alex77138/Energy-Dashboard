#pragma once
#include "types.h"
void webserver_start();
void webserver_log(const AppData &d);  // appeler depuis poll_task pour les graphiques
