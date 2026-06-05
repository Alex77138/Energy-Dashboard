// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Alexandre Richard -- https://github.com/Alex77138/Energy-Dashboard
#include "display.h"
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#define TOUCH_MODULES_GT911
#include <TouchLib.h>

// IDF 5.x: esp_cache_msync remplace Cache_WriteBack_Addr (dépréciée)
extern "C" esp_err_t esp_cache_msync(void *addr, size_t size, int flags);
#define ESP_CACHE_MSYNC_FLAG_DIR_C2M   (1 << 2)
#define ESP_CACHE_MSYNC_FLAG_UNALIGNED (1 << 1)

#define TFT_BL    2
#define TOUCH_SDA 19
#define TOUCH_SCL 20
#define TOUCH_RST 38

static uint8_t    s_rotation = 0;
static TouchLib  *s_touch    = nullptr;

// bounce_buffer_size_px=0 : sans bounce buffer, l'ISR VSYNC (IRAM_ATTR) ne lit
// plus la PSRAM → plus de crash "Cache disabled but cached memory region accessed"
// lors des écritures NVS/flash qui désactivent le cache via IPC.
static Arduino_ESP32RGBPanel *_bus = new Arduino_ESP32RGBPanel(
    40, 41, 39, 42,             /* DE, VSYNC, HSYNC, PCLK */
    45, 48, 47, 21, 14,         /* R0-R4 */
    5, 6, 7, 15, 16, 4,         /* G0-G5 */
    8, 3, 46, 9, 1,             /* B0-B4 */
    0, 8, 4, 8,                 /* hsync_pol, hfp, hpw, hbp */
    0, 8, 4, 8,                 /* vsync_pol, vfp, vpw, vbp */
    1, 16000000, false,         /* pclk_active_neg, freq, useBigEndian */
    0, 0, 0                     /* de_idle_high, pclk_idle_high, bounce_buffer_size_px */
);

static Arduino_RGB_Display *_gfx = new Arduino_RGB_Display(
    800, 480, _bus, 0, false    /* w, h, bus, rotation, auto_flush */
);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t s_buf1[800 * 20];
static lv_color_t s_buf2[800 * 20];
static uint16_t  *s_fb = nullptr; // pointeur framebuffer PSRAM, initialisé dans display_init

// ─── Touch driver (TouchLib + GT911) ─────────────────────────────────────────
static void touch_read_cb(lv_indev_drv_t *, lv_indev_data_t *data) {
    if (!s_touch || !s_touch->read()) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    TP_Point p = s_touch->getPoint(0);
    data->point.x = (lv_coord_t)p.x;
    data->point.y = (lv_coord_t)p.y;
    data->state   = LV_INDEV_STATE_PR;
}

static bool gt911_init() {
    // Reset matériel via RST
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW);
    delay(200);
    digitalWrite(TOUCH_RST, HIGH);
    delay(200);

    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    Wire.setClock(400000);

    // Probe I2C pour trouver l'adresse réelle
    uint8_t addr = 0;
    Wire.beginTransmission(0x5D);
    if (Wire.endTransmission() == 0) { addr = 0x5D; }
    else {
        Wire.beginTransmission(0x14);
        if (Wire.endTransmission() == 0) { addr = 0x14; }
    }
    if (!addr) {
        Serial.println("[touch] GT911 absent sur I2C (0x5D et 0x14)");
        return false;
    }
    Serial.printf("[touch] GT911 trouve addr=0x%02X\n", addr);

    // init() de TouchLib fait un soft-reset et configure le chip
    // (retourne false quand rst=-1 — on ignore, le probe I2C suffit)
    s_touch = new TouchLib(Wire, TOUCH_SDA, TOUCH_SCL, addr);
    s_touch->init();
    return true;
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    const int16_t w = area->x2 - area->x1 + 1;
    const int16_t h = area->y2 - area->y1 + 1;
    _gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)color_p, w, h);
    // Le DMA lit la PSRAM directement (bypass cache CPU) : flush immédiat de
    // chaque tile pour éviter que le DMA affiche des pixels périmés → jitter.
    if (s_fb) {
        esp_cache_msync(s_fb + (uint32_t)area->y1 * 800,
                        (uint32_t)h * 800 * 2,
                        ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    }
    lv_disp_flush_ready(drv);
}

bool display_init(uint8_t rotation) {
    Serial.println("[display] init...");
    _gfx->begin();
    s_fb = _gfx->getFramebuffer();

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    _gfx->fillScreen(0x0000);

    lv_init();
    lv_disp_draw_buf_init(&draw_buf, s_buf1, s_buf2, 800 * 20);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = 800;
    disp_drv.ver_res  = 480;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &draw_buf;
    if (rotation == 2) {
        disp_drv.sw_rotate = 1;
        disp_drv.rotated   = LV_DISP_ROT_180;
    }
    lv_disp_drv_register(&disp_drv);

    s_rotation = rotation;
    if (gt911_init()) {
        Serial.println("[touch] GT911 OK");
        static lv_indev_drv_t indev_drv;
        lv_indev_drv_init(&indev_drv);
        indev_drv.type    = LV_INDEV_TYPE_POINTER;
        indev_drv.read_cb = touch_read_cb;
        lv_indev_drv_register(&indev_drv);
    } else {
        Serial.println("[touch] GT911 non detecte");
    }

    Serial.printf("[display] OK rotation=%d\n", rotation);
    return true;
}

void display_tick() {
    static uint32_t last_tick = 0;
    uint32_t now = millis();
    lv_tick_inc(now - last_tick);
    last_tick = now;
    lv_timer_handler();
}
