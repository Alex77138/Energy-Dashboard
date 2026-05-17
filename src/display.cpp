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

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    const int16_t w = area->x2 - area->x1 + 1;
    const int16_t h = area->y2 - area->y1 + 1;
    _gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)color_p, w, h);
    lv_disp_flush_ready(drv);
}

bool display_init(uint8_t rotation) {
    Serial.println("[display] init...");
    _gfx->begin();

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    _gfx->fillScreen(BLACK);

    lv_init();

    lv_disp_draw_buf_init(&draw_buf, s_buf1, s_buf2, 800 * 20);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = 800;
    disp_drv.ver_res      = 480;
    disp_drv.flush_cb     = flush_cb;
    disp_drv.draw_buf     = &draw_buf;
    // full_refresh=1 retiré : avec mises à jour partielles LVGL écrit moins de pixels
    // → moins d'overlap avec le DMA continu → moins de sautes de couleur
    if (rotation == 2) {
        disp_drv.sw_rotate = 1;
        disp_drv.rotated   = LV_DISP_ROT_180;
    }
    lv_disp_drv_register(&disp_drv);

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
