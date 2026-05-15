#pragma once
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lvgl.h>

// Configuration LovyanGFX pour Guition JC8048W550 (5.5" 800×480, ESP32-S3)
//
// IMPORTANT — ne jamais appeler setRotation() sur Panel_RGB : réinitialise le
// DMA et produit des couleurs défilantes. Rotation fixée à 0 à l'init.

class LGFX_JC8048W550 : public lgfx::LGFX_Device {
    lgfx::Panel_RGB _panel;
    lgfx::Bus_RGB   _bus;
    lgfx::Light_PWM _light;

public:
    LGFX_JC8048W550() {
        {
            auto cfg = _panel.config();
            cfg.memory_width  = 800;
            cfg.memory_height = 480;
            cfg.panel_width   = 800;
            cfg.panel_height  = 480;
            cfg.offset_x      = 0;
            cfg.offset_y      = 0;
            _panel.config(cfg);
        }
        {
            auto cfg = _panel.config_detail();
            cfg.use_psram = 1;
            _panel.config_detail(cfg);
        }
        {
            auto cfg = _bus.config();
            cfg.panel    = &_panel;
            // Bleu B0-B4
            cfg.pin_d0  = 8;  cfg.pin_d1  = 3;  cfg.pin_d2 = 46;
            cfg.pin_d3  = 9;  cfg.pin_d4  = 1;
            // Vert G0-G5
            cfg.pin_d5  = 5;  cfg.pin_d6  = 6;  cfg.pin_d7  = 7;
            cfg.pin_d8  = 15; cfg.pin_d9  = 16; cfg.pin_d10 = 4;
            // Rouge R0-R4
            cfg.pin_d11 = 45; cfg.pin_d12 = 48; cfg.pin_d13 = 47;
            cfg.pin_d14 = 21; cfg.pin_d15 = 14;
            // Sync
            cfg.pin_henable = 40;
            cfg.pin_vsync   = 41;
            cfg.pin_hsync   = 39;
            cfg.pin_pclk    = 42;
            cfg.freq_write  = 14000000;
            // Timings JC8048W550 — 14 MHz, pclk_idle_high=1
            cfg.hsync_polarity    = 0;
            cfg.hsync_front_porch = 8;
            cfg.hsync_pulse_width = 4;
            cfg.hsync_back_porch  = 43;
            cfg.vsync_polarity    = 0;
            cfg.vsync_front_porch = 8;
            cfg.vsync_pulse_width = 4;
            cfg.vsync_back_porch  = 12;
            cfg.pclk_idle_high    = 1;
            _bus.config(cfg);
        }
        _panel.setBus(&_bus);
        {
            auto cfg = _light.config();
            cfg.pin_bl      = 2;
            cfg.invert      = false;
            cfg.freq        = 44100;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        setPanel(&_panel);
    }
};

extern LGFX_JC8048W550 gfx;

bool display_init(uint8_t rotation = 0);
void display_tick();
