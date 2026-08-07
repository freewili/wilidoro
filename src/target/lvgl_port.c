#include "lvgl_port.h"
#include "lvgl.h"
#include "fw2.h"
#include "pico/stdlib.h"

#define DISP_HOR 480
#define DISP_VER 320
#define BUF_LINES 30
#define BUF_PX (DISP_HOR * BUF_LINES)
static lv_color_t s_buf1[BUF_PX];

static uint32_t tick_get_cb(void) { return to_ms_since_boot(get_absolute_time()); }

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    lv_draw_sw_rgb565_swap(px_map, (uint32_t)w * h);
    st7796_blit_rect((uint16_t)area->x1, (uint16_t)area->y1,
                     (uint16_t)area->x2, (uint16_t)area->y2, (const uint16_t *)px_map);
    lv_display_flush_ready(disp);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    uint16_t x, y;
    if (ft6336_poll(&x, &y)) {
        data->point.x = x; data->point.y = y; data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void lvgl_port_init(void) {
    lv_init();
    lv_tick_set_cb(tick_get_cb);
    lv_display_t *disp = lv_display_create(DISP_HOR, DISP_VER);
    lv_display_set_flush_cb(disp, flush_cb);
    /* The PSRAM app's initial stack is fixed at 0x20070000. A second draw
       buffer pushed static SRAM beyond that address, corrupting startup
       before main could reach the display. Flushes are synchronous, so one
       30-line partial buffer is sufficient and leaves a real stack margin. */
    lv_display_set_buffers(disp, s_buf1, NULL, sizeof(s_buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
}

void lvgl_port_register_touch(void) {
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
}
