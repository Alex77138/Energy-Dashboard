#pragma once
#include "types.h"
#include <stddef.h>

bool     sd_init();
bool     sd_ready();
uint64_t sd_used_bytes();
uint64_t sd_total_bytes();
void sd_log(const AppData &d);
bool sd_get_history(char *out, size_t out_sz, int count);

// Données journalières (baselines multi-source)
bool sd_save_daily(int yday, float grid_base, const float *solar_base, int n_solar,
                   const float *router_base, int n_router);
bool sd_load_daily(int *yday, float *grid_base, float *solar_base, int n_solar,
                   float *router_base, int n_router);

// Données hebdo/mensuelles (bases cumulées)
struct PeriodData {
    int32_t week_year  = 0;
    int32_t week_num   = 0;
    int32_t month_year = 0;
    int32_t month_num  = 0;
    float grid_week    = 0;
    float grid_month   = 0;
    float solar_week[MAX_SOLAR]  = {};
    float solar_month[MAX_SOLAR] = {};
    float bat_week[MAX_BATTERIES]  = {};
    float bat_month[MAX_BATTERIES] = {};
    float rtr_week[MAX_ROUTERS]    = {};
    float rtr_month[MAX_ROUTERS]   = {};
    float rtr_dur_week[MAX_ROUTERS]  = {};
    float rtr_dur_month[MAX_ROUTERS] = {};
};
bool sd_save_period(const PeriodData &pd);
bool sd_load_period(PeriodData &pd);

// Ring buffer journalier (graphiques historiques)
bool sd_save_day_ring(int yday, int count, int head, int32_t last_ts,
                      const void *data, size_t data_sz);
bool sd_load_day_ring(int *yday, int *count, int *head, int32_t *last_ts,
                      void *data, size_t data_sz);
