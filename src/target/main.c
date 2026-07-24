#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "bl_pwm.h"

int main(void) {
    board_init();                 // 250 MHz, clk_peri re-source, I2C1, ioexp
    st7796_init();
    st7796_fill_screen(0x0000);   // black, no boot garbage

    lvgl_port_init();
    ft6336_init();
    lvgl_port_register_touch();

    ws2812_init(pio1, (uint)pio_claim_unused_sm(pio1, true), PIN_LED_DATA);
    ws2812_set_brightness(40);
    ws2812_clear();
    ws2812_show();

    // blank screen with a centered label to prove LVGL renders
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0C0C12), LV_PART_MAIN);
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "wilidoro");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFF5B45), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(label);

    lv_timer_handler();           // render first frame
    bl_pwm_init();                // backlight PWM on AFTER first frame (reassigns GPIO25)
    DIAG("wilidoro up: sys=%u kHz\n", BOARD_SYS_CLOCK_KHZ);

    absolute_time_t next_led = get_absolute_time();
    for (;;) {
        lv_timer_handler();
        if (absolute_time_diff_us(get_absolute_time(), next_led) <= 0) {
            ws2812_show();        // re-latch (BSP first-show quirk)
            next_led = make_timeout_time_ms(250);
        }
        sleep_ms(5);
    }
}
