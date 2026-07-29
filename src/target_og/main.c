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
#include "hardware/pio.h"
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

    /* pio0 sm0 -- the BSP's documented allocation (sm1 PDM, sm2 I2S). This is
       also what makes fwog_power_poll()'s red-hold countdown visible: it only
       paints the bar if ws2812_ready(). */
    const bool leds_ok = ws2812_init(pio0, 0);
    if (!leds_ok) DIAG("[wilidoro] ws2812 init FAILED\n");

    /* pio0 sm2 -- the BSP's documented allocation, sharing the block with
       WS2812 on sm0. */
    const bool audio_ok = i2s_audio_init(pio0, 2);
    if (!audio_ok) DIAG("[wilidoro] i2s init FAILED\n");

    /* The LIS3DH is brought up inside hal_init() (src/hal/hal_og.c), not
       here -- that is what lets hal_caps().imu report the real outcome
       (lis3dh_configure()'s own success/failure) to screen_settings.c's
       "no imu" string instead of the HAL hardcoding true regardless of
       whether the part actually came up. lis3dh_configure() DIAGs on
       failure itself (whoami mismatch or I2C fault); the heartbeat below
       reads hal_caps().imu rather than keeping a second local copy of the
       same bool. */

    lvgl_port_og_init();
    app_init();               /* builds screens, starts the tick timer */
    lv_mem_monitor_t mm0; lv_mem_monitor(&mm0);
    DIAG("[wilidoro] lvgl heap after init: free=%u max_used=%u frag_pct=%u\n",
         (unsigned)mm0.free_size, (unsigned)mm0.max_used, (unsigned)mm0.frag_pct);
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
            lv_mem_monitor_t mm; lv_mem_monitor(&mm);
            DIAG("[wilidoro] alive (panel=%s leds=%s audio=%s imu=%s) heap free=%u max_used=%u frag_pct=%u\n",
                 panel_ok ? "ok" : "FAILED", leds_ok ? "ok" : "FAILED", audio_ok ? "ok" : "FAILED",
                 hal_caps().imu ? "ok" : "FAILED",
                 (unsigned)mm.free_size, (unsigned)mm.max_used, (unsigned)mm.frag_pct);
        }
        sleep_ms(2);
    }
}
