#pragma once
#include "types.h"
#include <stddef.h>

bool sd_init();
bool sd_ready();
void sd_log(const AppData &d);
bool sd_get_history(char *out, size_t out_sz, int count);
