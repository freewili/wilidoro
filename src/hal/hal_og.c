/* FreeWili OG display-CPU HAL backend.
 *
 * The OG's five coloured buttons map 1:1 onto the softkey columns the app
 * already routes (grey..red == cols 0..4). The FreeWili 2's D-pad, HOME, OK,
 * CANCEL and PAGE have no counterpart and are never emitted; the screens
 * spend a softkey column on Back instead. */
#include "hal.h"
#include "fwog_display.h"
#include "pico/stdlib.h"
#include "tone_synth.h"

#define BTN_QUEUE_LEN 8
static hal_btn_t s_queue[BTN_QUEUE_LEN];
static uint8_t   s_head, s_tail;
static bool      s_power_armed;

/* This board has no ambient-light sensor, so hal_lux() always returns false
   (see below) and app.c's sensor_cb auto-dim block -- the only caller of
   hal_led_brightness() anywhere in the app -- never runs on the OG. Without a
   seed here s_led_bright would keep its 255 static initializer forever and
   the LED ring would run uncapped. 40 mirrors LED_BRIGHT_MAX in src/app/app.c,
   the FreeWili 2's bench-chosen indoor-comfort ceiling for its own (16-LED)
   chain; the HAL deliberately does not #include app.c's header to reach that
   macro, since it is app-layer, so this is its own constant kept in sync by
   comment rather than by reference. Unlike the FW2, this is a fixed cap, not
   a bright-room maximum that auto-dim can scale lower -- see hardware-notes.md. */
#define FWOG_LED_BRIGHT_DEFAULT 40u

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
    hal_led_brightness((uint8_t)FWOG_LED_BRIGHT_DEFAULT);
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

    i2s_audio_process();   /* feeds the A/B chain; starves and truncates without it */
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
/* One note at a time. i2s_audio_start() takes a caller-owned buffer and reads
   from it while the note plays, so this must stay alive until the note ends --
   hence static, not a stack array. 3200 int16 is 6.4 KB. */
static int16_t s_tone_buf[TONE_MAX_SAMPLES];

void hal_tone(uint16_t hz, uint16_t ms, uint8_t amp) {
    if (ms == 0u) return;
    i2s_audio_stop();                       /* always stop before re-arming */
    const size_t n = tone_render(hz, ms, amp, s_tone_buf, TONE_MAX_SAMPLES);
    if (n == 0u) return;
    /* force_mono=true, is_8bit=false: our samples are 16-bit mono. */
    (void)i2s_audio_start(s_tone_buf, (unsigned)n, true, false);
}

void hal_audio_idle(void) {
    i2s_audio_stop();
}

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
    c.audio   = true;
    c.imu     = false;   /* Plan OG-B */
    c.radio   = false;   /* Plan OG-D */
    return c;
}
