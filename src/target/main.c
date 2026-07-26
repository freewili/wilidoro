#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "hal.h"
#include "app.h"

int main(void) {
    /* 250 MHz (the board default) keeps the NAU88C10 sample rate at exactly the
       16009 Hz that Plan C2's audio was hardware-verified at. The DVI pixel clock
       is clk_sys/10 = 25.0 MHz, 0.7 % below the 640x480p60 standard 25.175 MHz.
       If a display refuses to sync, board_init_clk(252000) gives an exact
       25.2 MHz at the cost of ~0.8 % audio pitch (fs -> 16137 Hz) -- safe to
       switch because hal_target.c derives the sample rate from clk_sys at
       runtime rather than hardcoding it. */
    board_init();
    st7796_init();
    st7796_fill_screen(0x0000);
    lvgl_port_init();
    ft6336_init();
    lvgl_port_register_touch();
    hal_init();               /* uartkbd + ws2812 + bl_pwm */
    app_init();               /* builds screens, starts tick */
    lv_timer_handler();       /* first frame */
    /* Report the ACTUAL clock, not BOARD_SYS_CLOCK_KHZ -- that macro is the
       compile-time 250000 default, so it would silently lie if this app ever
       switched to board_init_clk() (see the note above main's board_init call). */
    DIAG("wilidoro app up: sys=%u kHz\n", (unsigned)(clock_get_hz(clk_sys) / 1000u));
    for (;;) {
        hal_pump();           /* drain uartkbd every iteration */
        lv_timer_handler();
        sleep_ms(5);
    }
}
