/* FreeWili OG display-CPU HAL backend.
 *
 * The OG's five coloured buttons map 1:1 onto the softkey columns the app
 * already routes (grey..red == cols 0..4). The FreeWili 2's D-pad, HOME, OK,
 * CANCEL and PAGE have no counterpart and are never emitted; the screens
 * spend a softkey column on Back instead. */
#include "hal.h"
#include "fwog_display.h"
#include "pico/stdlib.h"

#define BTN_QUEUE_LEN 8
static hal_btn_t s_queue[BTN_QUEUE_LEN];
static uint8_t   s_head, s_tail;
static bool      s_power_armed;

static void queue_push(hal_btn_t b) {
    uint8_t next = (uint8_t)((s_head + 1u) % BTN_QUEUE_LEN);
    if (next == s_tail) return;          /* full: drop, never overwrite */
    s_queue[s_head] = b;
    s_head = next;
}

void hal_init(void) {
    s_head = s_tail = 0;
    s_power_armed = false;
    /* board_init() is called by main() before hal_init(); it owns the panel,
       the I2C bus, the buttons and the backlight PWM divider. */
    hal_backlight(100);
}

/* Once per main-loop iteration. fwog_power_poll() is THE button read: calling
   fwog_buttons_poll() again here would consume edges out from under the
   ship-mode hold machine, which depends on that debounce state. */
void hal_pump(void) {
    const fwog_power_t p = fwog_power_poll(hal_now_ms());
    s_power_armed = p.armed;

    /* fwog_btn_id_t is GRAY,YELLOW,GREEN,BLUE,RED == 0..4, and hal_btn_t is
       GREY,YELLOW,GREEN,BLUE,RED == 0..4. Same order, same colours. */
    for (unsigned i = 0; i < FWOG_BTN_COUNT; i++) {
        if (p.buttons.pressed & (1u << i)) queue_push((hal_btn_t)i);
    }
}

uint32_t hal_now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

bool hal_next_button(hal_btn_t *out) {
    if (s_tail == s_head) return false;
    *out = s_queue[s_tail];
    s_tail = (uint8_t)((s_tail + 1u) % BTN_QUEUE_LEN);
    return true;
}

/* True while a shutdown countdown is running. fwog_power_poll() paints that
   countdown onto the WS2812 bar itself and restores the previous colours if
   the hold is aborted, so the app must not write LEDs during it. */
bool hal_power_armed(void) { return s_power_armed; }

/* ---- LEDs: Plan OG-B ---- */
/* The OG's WS2812 driver has no brightness control -- the FreeWili 2's
   ws2812_set_brightness() has no counterpart here -- so scale in software on
   the way in and keep the chain's own values at what we want lit. */
static uint8_t s_led_bright = 255;
static uint8_t s_led_rgb[FWOG_LED_COUNT][3];

int hal_led_count(void) { return FWOG_LED_COUNT; }

void hal_led_set(int i, uint8_t r, uint8_t g, uint8_t b) {
    if (i < 0 || i >= FWOG_LED_COUNT) return;
    s_led_rgb[i][0] = r; s_led_rgb[i][1] = g; s_led_rgb[i][2] = b;
}

void hal_led_brightness(uint8_t level) { s_led_bright = level; }

void hal_led_show(void) {
    if (!ws2812_ready()) return;
    for (unsigned i = 0; i < FWOG_LED_COUNT; i++) {
        ws2812_set_color(i,
            (uint8_t)(((unsigned)s_led_rgb[i][0] * s_led_bright) / 255u),
            (uint8_t)(((unsigned)s_led_rgb[i][1] * s_led_bright) / 255u),
            (uint8_t)(((unsigned)s_led_rgb[i][2] * s_led_bright) / 255u));
    }
    ws2812_process();   /* blocking, DMA-free: once per show, never in a spin */
}

/* ---- Audio: Plan OG-B ---- */
void hal_tone(uint16_t hz, uint16_t ms, uint8_t amp) { (void)hz;(void)ms;(void)amp; }
void hal_audio_idle(void) { }

/* ---- Backlight: no light sensor on this board, so this is a fixed level ----
 * hal.h's hal_backlight() takes a 0..100 percent, but board_backlight() takes
 * a 0-255 PWM duty directly (wiliOGbsp/bsp/display_cpu/platform/board.h: "0-255,
 * as PWM duty"; FWOG_BACKLIGHT_WRAP is 255 in board.c). Passing the percent
 * straight through under-drives the backlight to ~39% of full brightness --
 * scale it. */
void hal_backlight(uint8_t pct) {
    if (pct > 100u) pct = 100u;
    board_backlight((uint8_t)((unsigned)pct * 255u / 100u));
}

/* ---- Absent hardware ---- */
bool hal_dvi_surface(hal_dvi_surface_t *s) { (void)s; return false; }   /* no HSTX on RP2040 */
void hal_dvi_enable(bool on) { (void)on; }
bool hal_lux(float *lux) { (void)lux; return false; }                   /* no ambient sensor */

/* ---- IMU: Plan OG-B ---- */
bool hal_imu(float *ax, float *ay, float *az) { (void)ax;(void)ay;(void)az; return false; }

/* ---- Beacon: Plan OG-D, over the inter-CPU link ---- */
void hal_beacon_tx(const uint8_t wire[BEACON_WIRE_LEN]) { (void)wire; }
bool hal_beacon_rx(uint8_t wire[BEACON_WIRE_LEN]) { (void)wire; return false; }

hal_caps_t hal_caps(void) {
    hal_caps_t c = {0};   /* light, dvi stay false: no hardware on this board */
    c.buttons = true;
    c.leds    = true;
    c.audio   = false;   /* Plan OG-B */
    c.imu     = false;   /* Plan OG-B */
    c.radio   = false;   /* Plan OG-D */
    return c;
}
