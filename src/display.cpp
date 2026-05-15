#include "display.h"

LGFX_JC8048W550 gfx;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t s_buf1[800 * 20];
static lv_color_t s_buf2[800 * 20];

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    const uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
    const uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);

    static bool first = true;
    if (first) {
        Serial.printf("[flush] 1er appel x=%d y=%d w=%u h=%u\n", area->x1, area->y1, w, h);
        first = false;
    }

    // pushImage déclenche endWrite() → display() → Cache_WriteBack_Addr() via _auto_display.
    gfx.pushImage(area->x1, area->y1, w, h, (lgfx::rgb565_t*)color_p);
    lv_disp_flush_ready(drv);
}

bool display_init(uint8_t rotation) {
    (void)rotation;

    Serial.println("[display] init...");
    if (!gfx.init()) {
        Serial.println("[display] gfx.init ECHEC — PSRAM ou panel?");
        return false;
    }
    Serial.println("[display] gfx.init OK");
    gfx.setBrightness(200);

    // Diagnostic RGB via LovyanGFX — endWrite() → display() → Cache_WriteBack_Addr auto
    Serial.println("[display] TEST Rouge...");
    gfx.fillScreen(TFT_RED);   delay(800);
    Serial.println("[display] TEST Vert...");
    gfx.fillScreen(TFT_GREEN); delay(800);
    Serial.println("[display] TEST Bleu...");
    gfx.fillScreen(TFT_BLUE);  delay(800);
    gfx.fillScreen(TFT_BLACK);
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
