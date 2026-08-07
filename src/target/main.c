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
    /* fw2_psram_app's SRAM bootstrap has already run board_init_psram().
       board_init() recognizes that inherited state and only completes normal
       peripheral ownership; it must still precede recovery and every driver. */
    DIAG("wilidoro: main\n");
    board_init();
    DIAG("wilidoro: board ready\n");
    fw2_app_recovery_init();
    DIAG("wilidoro: recovery ready\n");
    st7796_init();
    DIAG("wilidoro: lcd ready\n");
    agentio_init();
    DIAG("wilidoro: agentio ready\n");
    st7796_fill_screen(0x0000);
    fw2_app_about_use_lcd();
    lvgl_port_init();
    DIAG("wilidoro: lvgl ready\n");
    ft6336_init();
    lvgl_port_register_touch();
    DIAG("wilidoro: touch ready\n");
    hal_init();               /* uartkbd + ws2812 + bl_pwm */
    DIAG("wilidoro: hal ready\n");
    app_init();               /* builds screens, starts tick */
    DIAG("wilidoro: app ready\n");
    lv_timer_handler();       /* first frame */
    /* Report the ACTUAL clock, not BOARD_SYS_CLOCK_KHZ -- that macro is the
       compile-time 250000 default, so it would silently lie if this app ever
       switched to a different inherited board clock. */
    DIAG("wilidoro app up: sys=%u kHz\n", (unsigned)(clock_get_hz(clk_sys) / 1000u));
    for (;;) {
        fw2_app_recovery_task();
        hal_pump();           /* AgentIO + non-input hardware service */
        lv_timer_handler();
        sleep_ms(5);
    }
}
