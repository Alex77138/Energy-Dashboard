#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "device_config.h"

// Base commune à GridData et SolarData (même layout mémoire initial)
struct MeasureData {
    float power_w   = 0;
    float today_kwh = 0;
    bool  online    = false;
};

struct GridData : public MeasureData {};

struct SolarData : public MeasureData {
    float dc_voltage = 0;   // OpenDTU uniquement
    int   limit_pct  = 100; // OpenDTU uniquement
};

// Alias de compatibilité pour f1atb.h (routeur non utilisé mais doit compiler)
using ShellyEMData = MeasureData;

struct RouterData {
    float power_w    = 0;   // puissance routée (W)
    float triac_pct  = 0;   // % ouverture triac (0–100)
    float today_kwh  = 0;
    float duration_h = 0;   // durée équivalente (heures décimales)
    bool  active     = false;
    bool  forced     = false;
    bool  online     = false;
};

struct BatteryData {
    float power_w  = 0;   // positif = décharge, négatif = charge
    float soc_pct  = 0;
    bool  online   = false;
};

struct AppData {
    GridData    grid;
    SolarData   solar;
    BatteryData batteries[MAX_BATTERIES];
    RouterData  routers[MAX_ROUTERS];
    SemaphoreHandle_t mutex = nullptr;
};

extern AppData g_data;
