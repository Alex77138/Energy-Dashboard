#pragma once
#include <lvgl.h>
#include "types.h"

void ui_create();
void ui_update(const AppData &d);
void ui_navigate(int screen_id);          // 0=main 1=solar 2=grid 3=bat 4=router (core 0 only)
void ui_navigate_request(int screen_id);  // thread-safe, depuis n'importe quelle tâche
void ui_process_nav_request();            // à appeler depuis display_task
void ui_auto_return_check();              // retour automatique écran principal après 30 s
