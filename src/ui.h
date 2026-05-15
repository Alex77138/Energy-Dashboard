#pragma once
#include <lvgl.h>
#include "types.h"

void ui_create();
void ui_update(const AppData &d);
void ui_navigate(bool left); // true=page suivante, false=page précédente
