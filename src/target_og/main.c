/* FreeWili OG display CPU: wilidoro entry point.
 *
 * Plan A brings the board up and proves the flash path. LVGL lands in Task 4
 * (this file draws one label to prove the panel, flush path and font all
 * work); the real app loop arrives in Task 5. */
#include "fwog_display.h"
#include "hal.h"
#include "lvgl.h"
#include "lvgl_port_og.h"
#include "pico/stdlib.h"

/* Red held 6 s powers the board off, countdown on the WS2812 bar. board_init()
   references a symbol only this macro defines -- an app declaring no policy
   does not link. */
FWOG_POWER_DEFAULT();

int main(void) {
    board_init();
    hal_init();

    /* Bounded, not unbounded. st7789_init_begin() can fail to leave
       ST7789_INIT_IDLE when clk_peri cannot reach the panel rate, which makes
       st7789_init_step() a permanent no-op -- and this CPU has no watchdog to
       recover a spin. 500 ms is comfortably more than the ~125 ms a healthy
       init needs. Do not remove the deadline. */
    st7789_init_begin();
    const absolute_time_t lcd_deadline = make_timeout_time_ms(500);
    while (!st7789_ready() && !time_reached(lcd_deadline)) st7789_init_step();
    const bool lcd_ready = st7789_ready();
    if (!lcd_ready) {
        DIAG("[wilidoro] st7789 init FAILED\n");
    } else {
        st7789_clear(0x0000u);           /* wipe the bootloader's leftover UI */
        st7789_dma_wait();
    }
    /* This early DIAG is frequently dropped -- USB CDC has not enumerated yet
       at this point in boot (board_init()'s own DIAG suffers the same fate,
       see the FreeWili OG lcd_display app for the documented precedent), so
       the 1 Hz heartbeat below repeats the panel status where it is
       reliably observed. */

    lvgl_port_og_init();

    /* A label proves the panel, the flush path and the font all work. */
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101014), 0);
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl, "25:00");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFF5B45), 0);
    lv_obj_center(lbl);

    absolute_time_t next_beat = make_timeout_time_ms(1000);
    while (true) {
        hal_pump();                      /* calls fwog_power_poll() exactly once */

        hal_btn_t b;
        while (hal_next_button(&b)) {
            DIAG("[wilidoro] button %d\n", (int)b);
        }

        if (time_reached(next_beat)) {
            next_beat = make_timeout_time_ms(1000);
            DIAG("[wilidoro] display alive (panel=%s)\n",
                 lcd_ready ? "ok" : "FAILED");
        }
        lv_timer_handler();
        sleep_ms(2);
    }
}
