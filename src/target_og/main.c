/* FreeWili OG display CPU: wilidoro entry point.
 *
 * Task 4 brought the board up, proved the flash path and proved the panel,
 * flush path and font all work with a standalone label. Task 5 hands the
 * screen to the real app: app_init() builds the neon timer face and starts
 * the LVGL timer that drives it, so the loop here only pumps the HAL and
 * LVGL -- this mirrors src/target/main.c exactly. */
#include "app.h"
#include "fwog_display.h"
#include "hal.h"
#include "hardware/clocks.h"
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
    const bool panel_ok = st7789_ready();
    if (!panel_ok) {
        DIAG("[wilidoro] st7789 init FAILED\n");
    } else {
        st7789_clear(0x0000u);           /* wipe the bootloader's leftover UI */
        st7789_dma_wait();
    }

    lvgl_port_og_init();
    app_init();               /* builds screens, starts the tick timer */
    lv_timer_handler();       /* first frame */
    DIAG("wilidoro OG up: sys=%u kHz\n", (unsigned)(clock_get_hz(clk_sys) / 1000u));

    /* 1 Hz heartbeat carrying the panel status. A boot-time-only DIAG lands in
       the window before USB CDC enumerates and pico_stdio_usb drops it -- this
       is how Task 4 first noticed a bounded init timeout would otherwise be a
       black panel with a silent console. Repeating it at 1 Hz makes that
       failure observable whenever the console happens to attach. */
    absolute_time_t next_beat = make_timeout_time_ms(1000);

    for (;;) {
        hal_pump();            /* the one fwog_power_poll() per iteration */
        lv_timer_handler();
        if (time_reached(next_beat)) {
            next_beat = make_timeout_time_ms(1000);
            DIAG("[wilidoro] alive (panel=%s)\n", panel_ok ? "ok" : "FAILED");
        }
        sleep_ms(2);
    }
}
