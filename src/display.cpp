#include "display.h"
#include <Arduino_GFX_Library.h>

#define TFT_BL 2

static Arduino_ESP32RGBPanel *_bus = new Arduino_ESP32RGBPanel(
    GFX_NOT_DEFINED, GFX_NOT_DEFINED, GFX_NOT_DEFINED,
    40 /* DE */, 41 /* VSYNC */, 39 /* HSYNC */, 42 /* PCLK */,
    45, 48, 47, 21, 14, /* R0-R4 */
    5, 6, 7, 15, 16, 4, /* G0-G5 */
    8, 3, 46, 9, 1       /* B0-B4 */
);

static Arduino_RPi_DPI_RGBPanel *_gfx = new Arduino_RPi_DPI_RGBPanel(
    _bus,
    800, 0, 8, 4, 8,   /* width, hsync_pol, hfp, hpw, hbp */
    480, 0, 8, 4, 8,   /* height, vsync_pol, vfp, vpw, vbp */
    1, 16000000, true  /* pclk_active_neg, freq, auto_flush */
);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t s_buf1[800 * 20];
static lv_color_t s_buf2[800 * 20];
static uint8_t s_rotation = 0;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    const int16_t x1 = area->x1, y1 = area->y1;
    const int16_t w  = area->x2 - area->x1 + 1;
    const int16_t h  = area->y2 - area->y1 + 1;

    if (s_rotation == 2) {
        // 180° : inverser tous les pixels du tile (miroir X+Y) puis écrire aux coords opposées
        uint16_t *p = (uint16_t *)color_p;
        uint16_t *q = p + (int32_t)w * h - 1;
        while (p < q) { uint16_t t = *p; *p++ = *q; *q-- = t; }
        _gfx->draw16bitRGBBitmap(800 - x1 - w, 480 - y1 - h, (uint16_t *)color_p, w, h);
    } else {
        _gfx->draw16bitRGBBitmap(x1, y1, (uint16_t *)color_p, w, h);
    }
    lv_disp_flush_ready(drv);
}

bool display_init(uint8_t rotation) {
    s_rotation = rotation;

    Serial.println("[display] init Arduino_GFX...");
    _gfx->begin();
    Serial.println("[display] gfx->begin() OK");

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    Serial.println("[display] TEST Rouge...");
    _gfx->fillScreen(RED);
    delay(3000);
    Serial.println("[display] TEST Vert...");
    _gfx->fillScreen(GREEN);
    delay(3000);
    Serial.println("[display] TEST Bleu...");
    _gfx->fillScreen(BLUE);
    delay(3000);
    _gfx->fillScreen(BLACK);
    Serial.println("[display] TEST couleurs OK");

    lv_init();
    Serial.println("[display] lv_init OK");

    lv_disp_draw_buf_init(&draw_buf, s_buf1, s_buf2, 800 * 20);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = 800;
    disp_drv.ver_res      = 480;
    disp_drv.flush_cb     = flush_cb;
    disp_drv.draw_buf     = &draw_buf;
    disp_drv.full_refresh = 0;
    lv_disp_drv_register(&disp_drv);

    Serial.println("[display] LVGL OK");
    return true;
}

void display_tick() {
    static uint32_t last_tick = 0;
    uint32_t now = millis();
    lv_tick_inc(now - last_tick);
    last_tick = now;
    lv_timer_handler();
}
