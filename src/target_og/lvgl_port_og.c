/* LVGL 9 <-> ST7789 on the FreeWili OG display CPU.
 *
 * Partial render mode with two 320x40 buffers. The flush is blocking, as on
 * the FreeWili 2: the panel shares SPI1, so exactly one bus owner at a time
 * is the simplest correct arrangement. */
#include "lvgl_port_og.h"
#include "lvgl.h"
#include "fwog_display.h"
#include "hal.h"
#include "ui.h"

_Static_assert(DISP_HOR == UI_W && DISP_VER == UI_H &&
               DISP_HOR == ST7789_W && DISP_VER == ST7789_H,
               "LVGL panel geometry, the UI layout geometry and the ST7789 "
               "driver's own geometry disagree");

#define BUF_LINES 40
#define BUF_PX    (DISP_HOR * BUF_LINES)

static lv_color_t s_buf1[BUF_PX];
static lv_color_t s_buf2[BUF_PX];

static uint32_t tick_cb(void) { return hal_now_ms(); }

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
    const uint16_t x = (uint16_t)area->x1;
    const uint16_t y = (uint16_t)area->y1;
    const uint16_t w = (uint16_t)(area->x2 - area->x1 + 1);
    const uint16_t h = (uint16_t)(area->y2 - area->y1 + 1);

    /* No explicit st7789_dma_wait() here: st7789_set_window() and
       st7789_blit() each call it internally already, and st7789_blit() is a
       synchronous chunked spi_write_blocking() (see wiliOGbsp's
       bsp/display_cpu/lcd/st7789.c), not a DMA-backed blit -- it has already
       fully drained the SPI bus by the time it returns. */
    st7789_set_window(x, y, w, h);
    st7789_blit((const uint16_t *)px, (size_t)w * (size_t)h);

    lv_display_flush_ready(disp);
}

void lvgl_port_og_init(void) {
    lv_init();
    lv_tick_set_cb(tick_cb);

    lv_display_t *disp = lv_display_create(DISP_HOR, DISP_VER);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, s_buf1, s_buf2, sizeof(s_buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
}
