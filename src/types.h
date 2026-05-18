#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "device_config.h"

struct MeasureData {
    float power_w   = 0;
    float voltage_v = 0;
    float current_a = 0;
    float today_kwh = 0;
    float week_kwh  = 0;
    float month_kwh = 0;
    bool  online    = false;
};

struct GridData : public MeasureData {};

struct SolarData : public MeasureData {
    float dc_voltage = 0;   // OpenDTU/AhoyDTU uniquement
    int   limit_pct  = 100; // OpenDTU/AhoyDTU uniquement
};

// Alias de compatibilité pour f1atb.h
using ShellyEMData = MeasureData;

struct RouterData {
    float power_w          = 0;
    float voltage_v        = 0;
    float current_a        = 0;
    float triac_pct        = 0;
    float today_kwh        = 0;
    float week_kwh         = 0;
    float month_kwh        = 0;
    float duration_h       = 0;
    float week_duration_h  = 0;
    float month_duration_h = 0;
    bool  active           = false;
    bool  forced           = false;
    bool  online           = false;
};

struct BatteryData {
    float power_w   = 0;
    float voltage_v = 0;
    float current_a = 0;
    float soc_pct   = 0;
    float today_kwh = 0;
    float week_kwh  = 0;
    float month_kwh = 0;
    bool  online    = false;
};

struct AppData {
    GridData    grid;
    SolarData   solar;                    // total agrégé (compatibilité)
    SolarData   solar_src[MAX_SOLAR];     // données par source
    BatteryData batteries[MAX_BATTERIES];
    RouterData  routers[MAX_ROUTERS];
    SemaphoreHandle_t mutex = nullptr;
};

extern AppData g_data;
