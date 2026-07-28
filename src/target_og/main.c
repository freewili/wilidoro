/* FreeWili OG display CPU: wilidoro entry point.
 *
 * Plan A brings the board up and proves the flash path. LVGL arrives in
 * Task 4; the app loop in Task 5. */
#include "fwog_display.h"
#include "pico/stdlib.h"

/* Red held 6 s powers the board off, countdown on the WS2812 bar. board_init()
   references a symbol only this macro defines -- an app declaring no policy
   does not link. */
FWOG_POWER_DEFAULT();

int main(void) {
    board_init();

    /* Poll fast, report slowly: sampling buttons once a second is too coarse
       for the debouncer the ship-mode hold is built on. */
    absolute_time_t next_beat = make_timeout_time_ms(1000);
    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        fwog_power_poll(now);

        if (time_reached(next_beat)) {
            next_beat = make_timeout_time_ms(1000);
            DIAG("[wilidoro] display alive\n");
        }
        sleep_ms(2);
    }
}
