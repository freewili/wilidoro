/* FreeWili OG display CPU: wilidoro entry point.
 *
 * Plan A brings the board up and proves the flash path. LVGL arrives in
 * Task 4; the app loop in Task 5. */
#include "fwog_display.h"
#include "hal.h"
#include "pico/stdlib.h"

/* Red held 6 s powers the board off, countdown on the WS2812 bar. board_init()
   references a symbol only this macro defines -- an app declaring no policy
   does not link. */
FWOG_POWER_DEFAULT();

int main(void) {
    board_init();
    hal_init();

    absolute_time_t next_beat = make_timeout_time_ms(1000);
    while (true) {
        hal_pump();                      /* calls fwog_power_poll() exactly once */

        hal_btn_t b;
        while (hal_next_button(&b)) {
            DIAG("[wilidoro] button %d\n", (int)b);
        }

        if (time_reached(next_beat)) {
            next_beat = make_timeout_time_ms(1000);
            DIAG("[wilidoro] display alive\n");
        }
        sleep_ms(2);
    }
}
