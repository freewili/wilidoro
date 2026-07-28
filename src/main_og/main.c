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
       (compared by image CRC32, so an unchanged image is skipped). */
    fwog_display_update_run();

    absolute_time_t next_beat = make_timeout_time_ms(1000);
    while (true) {
        if (time_reached(next_beat)) {
            next_beat = make_timeout_time_ms(1000);
            DIAG("[wilidoro] main alive\n");
        }
        sleep_ms(2);
    }
}
