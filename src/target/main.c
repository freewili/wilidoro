#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "hal.h"
#include "app.h"

int main(void) {
    board_init();
    st7796_init();
    st7796_fill_screen(0x0000);
    lvgl_port_init();
    ft6336_init();
    lvgl_port_register_touch();
    hal_init();               /* uartkbd + ws2812 + bl_pwm */
    app_init();               /* builds screens, starts tick */
    lv_timer_handler();       /* first frame */
    DIAG("wilidoro app up: sys=%u kHz\n", BOARD_SYS_CLOCK_KHZ);
    for (;;) {
        hal_pump();           /* drain uartkbd every iteration */
        lv_timer_handler();
        sleep_ms(5);
    }
}
