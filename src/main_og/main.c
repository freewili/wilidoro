/* FreeWili OG main CPU: wilidoro's radio half.
 *
 * In Plan A this exists for one reason -- fwog_display_update_run() is how the
 * display application reaches the display CPU, which has no BOOTSEL button and
 * must never be UF2-flashed directly. Plan OG-D adds the CC1101 beacon. */
#include "fwog_main.h"
#include "pico/stdlib.h"

int main(void) {
    board_init();

    /* Push the embedded display image if the display CPU's copy differs
       (compared by image CRC32, so an unchanged image is skipped). This also
       performs board_release_display() itself, so we must not call it. */
    const fwog_display_result_t disp = fwog_display_update_run();

    /* Announce the transfer result for the first 10 s rather than once. The
       handshake finishes before USB CDC has enumerated and the host has
       asserted DTR, and pico_stdio_usb DROPS anything written before then --
       a single DIAG here is written into the void every time. */
    const absolute_time_t announce_until = make_timeout_time_ms(10000);

    absolute_time_t next_beat = make_timeout_time_ms(1000);
    while (true) {
        /* REQUIRED. board_init() arms a 2 s watchdog and it is the only way
           to recover a hung main CPU on this board, so the BSP makes kicking
           it the app's job (bsp/main_cpu/watchdog/watchdog.h). Omitting this
           does not fail to build or fail to run -- main simply resets every
           2 s forever, taking the display with it through board_init()'s
           GUI_NRESET. That is measured, not feared: it is what this file did
           on the first hardware bring-up. */
        board_watchdog_kick();

        if (time_reached(next_beat)) {
            next_beat = make_timeout_time_ms(1000);
            if (!time_reached(announce_until)) {
                DIAG("[wilidoro] main alive, display: %s\n",
                     fwog_display_result_text(disp));
            } else {
                DIAG("[wilidoro] main alive\n");
            }
        }
        sleep_ms(2);
    }
}
