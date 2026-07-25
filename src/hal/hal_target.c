#include "hal.h"
#include "fw2.h"
#include "input/uartkbd.h"
#include "input/uartkbd_parse.h"
#include "leds/ws2812_driver.h"
#include "leds/led_color.h"
#include "bl_pwm.h"
#include "sensors/opt4001.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"

/* uartkbd_btn_t (uartkbd_parse.h) does NOT share hal_btn_t's (hal.h) d-pad
 * order: uartkbd numbers NAV_CENTER before NAV_UP/DOWN/LEFT/RIGHT (5..9),
 * while hal_btn_t numbers UP,DOWN,LEFT,RIGHT,CENTER (5..9) -- CENTER lands
 * on a different slot in each enum. The 5 softkeys (GREY..RED, 0..4) and the
 * trailing HOME/OK/CANCEL/PAGE (10..13) do line up, but a blind (hal_btn_t)
 * cast would silently swap UP/DOWN/LEFT/RIGHT/CENTER on the coprocessor's
 * live d-pad. Map explicitly instead. */
static bool map_btn(uartkbd_btn_t in, hal_btn_t *out) {
    switch (in) {
        case UARTKBD_BTN_GREY:       *out = HAL_BTN_GREY;   return true;
        case UARTKBD_BTN_YELLOW:     *out = HAL_BTN_YELLOW; return true;
        case UARTKBD_BTN_GREEN:      *out = HAL_BTN_GREEN;  return true;
        case UARTKBD_BTN_BLUE:       *out = HAL_BTN_BLUE;   return true;
        case UARTKBD_BTN_RED:        *out = HAL_BTN_RED;    return true;
        case UARTKBD_BTN_NAV_UP:     *out = HAL_BTN_UP;     return true;
        case UARTKBD_BTN_NAV_DOWN:   *out = HAL_BTN_DOWN;   return true;
        case UARTKBD_BTN_NAV_LEFT:   *out = HAL_BTN_LEFT;   return true;
        case UARTKBD_BTN_NAV_RIGHT:  *out = HAL_BTN_RIGHT;  return true;
        case UARTKBD_BTN_NAV_CENTER: *out = HAL_BTN_CENTER; return true;
        case UARTKBD_BTN_HOME:       *out = HAL_BTN_HOME;   return true;
        case UARTKBD_BTN_OK:         *out = HAL_BTN_OK;     return true;
        case UARTKBD_BTN_CANCEL:     *out = HAL_BTN_CANCEL; return true;
        case UARTKBD_BTN_PAGE:       *out = HAL_BTN_PAGE;   return true;
        default: return false;
    }
}

static bool s_light;

void hal_init(void) {
    uartkbd_init();
    ws2812_init(pio1, (uint)pio_claim_unused_sm(pio1, true), PIN_LED_DATA);
    ws2812_set_brightness(40);
    ws2812_clear();
    ws2812_show();
    bl_pwm_init();       /* backlight full-on; auto-dim is Plan C */
    s_light = opt4001_init();
}
void hal_pump(void) { uartkbd_task(); }

uint32_t hal_now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

bool hal_next_button(hal_btn_t *out) {
    uartkbd_event_t ev;
    while (uartkbd_next_event(&ev)) {
        if (ev.pressed && map_btn(ev.btn, out)) return true;
    }
    return false;
}

void hal_led_set(int i, uint8_t r, uint8_t g, uint8_t b) {
    if (i < 0 || i >= WS2812_NUM_PIXELS) return;
    rgb_t c = { r, g, b }; ws2812_set_pixel((uint)i, c);
}
void hal_led_brightness(uint8_t level) { ws2812_set_brightness(level); }
void hal_led_show(void) { ws2812_show(); }

void hal_tone(uint16_t hz, uint16_t ms, uint8_t amp) { (void)hz;(void)ms;(void)amp; }  /* Plan C */
void hal_audio_idle(void) {}

void hal_backlight(uint8_t pct) { bl_pwm_set(pct); }

bool hal_imu(float *ax, float *ay, float *az) { (void)ax;(void)ay;(void)az; return false; }  /* Plan C */
bool hal_lux(float *lux) { return s_light && opt4001_read(lux); }

void hal_beacon_tx(const uint8_t wire[BEACON_WIRE_LEN]) { (void)wire; }                       /* Plan C */
bool hal_beacon_rx(uint8_t wire[BEACON_WIRE_LEN]) { (void)wire; return false; }               /* Plan C */

hal_caps_t hal_caps(void) {
    hal_caps_t c = { .radio=false,.imu=false,.light=s_light,.audio=false,.buttons=true,.leds=true };
    return c;
}
