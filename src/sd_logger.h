#pragma once
#include "types.h"
#include <stddef.h>

bool     sd_init();
bool     sd_ready();
uint64_t sd_used_bytes();
uint64_t sd_total_bytes();
void sd_log(const AppData &d);
bool sd_get_history(char *out, size_t out_sz, int count);

// Persistence des données journalières (baselines et ring buffer)
bool sd_save_daily(int yday, float grid_base, float solar_base);
bool sd_load_daily(int *yday, float *grid_base, float *solar_base);
bool sd_save_day_ring(int yday, int count, int head, int32_t last_ts,
                      const void *data, size_t data_sz);
bool sd_load_day_ring(int *yday, int *count, int *head, int32_t *last_ts,
                      void *data, size_t data_sz);
