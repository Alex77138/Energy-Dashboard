#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

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
    float triac_pct  = 0;
    float today_kwh  = 0;
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
    BatteryData battery;
    SemaphoreHandle_t mutex = nullptr;
};

extern AppData g_data;
