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
#include "hardware/clocks.h"
#include "platform/diag.h"

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

/* ---------------- audio ----------------------------------------------------
 * One-shot tones over the BSP's looping DMA ring:
 *  - TONE_FRAMES frames * 4 bytes = 4096 B: power-of-two AND aligned to its own
 *    size, as audio_i2s_duplex_play_loop's read-ring requires.
 *  - The requested pitch is snapped to a whole number of sine cycles inside the
 *    buffer, so the ring wraps phase-continuously (no seam click). The grid is
 *    AUDIO_FS_HZ/TONE_FRAMES ~ 15.6 Hz; src/app/sound.c keeps every note >= 440 Hz
 *    so the resulting detune stays under ~2 %.
 *  - Duration is a deadline serviced by hal_pump(), never a busy-wait: hal_tone
 *    must not block the LVGL main loop.
 *  - AUDIO_FS_HZ is the REAL rate: MCLK is an integer PWM divide of the 250 MHz
 *    clk_sys (250e6/61 = 4.0984 MHz), so fs = 4.0984e6/256 = 16009 Hz, not 16000.
 *    audio_i2s_duplex_init() still takes the nominal 16000 (it derives the divider
 *    from it). See wilibsp/docs/hardware/facts.md, "Audio: lock LRCK to MCLK/256".
 *  - The speaker is 0.5 W (wilibsp AGENTS.md invariant 10). Every tone's amplitude
 *    is SCALED (not clamped, so the table keeps its dynamics) by TONE_AMP_CAP/255,
 *    and the output stage is powered down after AUDIO_IDLE_MS of silence.
 */
#define TONE_FRAMES    1024u
#define TONE_AMP_CAP   160u      /* of 255; tone_gen peaks at 28000/32768, so the
                                    ceiling is ~0.54 full scale. Comfortable-volume
                                    default, same spirit as LED_BRIGHT_MAX. */
#define AUDIO_IDLE_MS  1500u     /* silence before the speaker stage powers down */

/* The REAL I2S sample rate, derived at runtime from clk_sys (was hardcoded to
   the 250 MHz value, 16009). audio_i2s_duplex_init() picks an INTEGER MCLK PWM
   divider `ticks = clk_sys/(256*16000)`, so the codec actually runs at
   clk_sys/(ticks*256) -- 16009 Hz at 250 MHz, 16137 Hz at 252 MHz.
   tone_arm() snaps each tone to a WHOLE number of sine cycles against this
   value, and that whole-cycle property is the only reason the DMA loop seam is
   inaudible. Hardcoding it would reintroduce a per-loop click the moment the
   board clock changed -- e.g. the 252 MHz option for exact DVI pixel timing. */
static uint32_t s_audio_fs_hz = 16009u;

static uint32_t s_tone[TONE_FRAMES] __attribute__((aligned(4096)));
static bool     s_audio_ok;      /* codec answered at boot */
static bool     s_audio_awake;   /* output stage powered up */
static bool     s_tone_pending;  /* a tone/rest deadline is running */
static bool     s_idle_pending;  /* an idle power-down deadline is running */
static uint32_t s_tone_end_ms, s_idle_at_ms;

static void audio_wake(void) {
    if (s_audio_awake) return;
    codec_nau88c10_dac_mute(false);            /* clear the boot/idle soft-mute */
    codec_nau88c10_set_output(CODEC_OUT_SPEAKER);
    s_audio_awake = true;
}

static void audio_sleep(void) {
    if (!s_audio_awake) return;
    codec_nau88c10_speaker_low_power();         /* soft-mute + spk mute + 5V boost off */
    s_audio_awake = false;
}

/* Fill the ring with `cycles` whole periods and start the loop. */
static void tone_arm(uint16_t hz, uint8_t amp) {
    uint32_t cycles = ((uint32_t)hz * TONE_FRAMES + s_audio_fs_hz / 2u) / s_audio_fs_hz;
    if (cycles < 1u) cycles = 1u;
    if (cycles > TONE_FRAMES / 4u) cycles = TONE_FRAMES / 4u;   /* >=4 samples/period */
    float actual_hz = (float)cycles * (float)s_audio_fs_hz / (float)TONE_FRAMES;
    uint32_t eff = (uint32_t)amp * TONE_AMP_CAP / 255u;         /* 0.5 W ceiling */

    static int16_t mono[TONE_FRAMES];
    float phase = 0.0f;
    tone_gen_fill(mono, TONE_FRAMES, actual_hz, (float)s_audio_fs_hz, &phase);
    for (unsigned i = 0; i < TONE_FRAMES; i++) {
        uint16_t s = (uint16_t)(int16_t)(((int32_t)mono[i] * (int32_t)eff) / 255);
        s_tone[i] = ((uint32_t)s << 16) | s;    /* same sample on both I2S slots */
    }
    audio_wake();
    audio_i2s_duplex_play_loop(s_tone, TONE_FRAMES);
}

/* Deadline service, called from hal_pump() every main-loop iteration. */
static void audio_pump(uint32_t now) {
    if (s_tone_pending && (int32_t)(now - s_tone_end_ms) >= 0) {
        audio_i2s_duplex_play_stop();
        s_tone_pending = false;
        s_idle_pending = true;
        s_idle_at_ms   = now + AUDIO_IDLE_MS;
    }
    if (s_idle_pending && (int32_t)(now - s_idle_at_ms) >= 0) {
        audio_sleep();
        s_idle_pending = false;
    }
}

static bool s_light;

/* ---------------- DVI --------------------------------------------------
 * 640x480p60 over the HSTX block (GPIO 12-19). The stored video region is
 * 480x240, sized by the HSTX_VID_*_MAX compile definitions in the root
 * CMakeLists.txt -- those size framebuf[] at COMPILE time, so the runtime
 * hstx_dvi_init() arguments alone would not shrink it.
 * Pixel clock is clk_sys/10 = 25.0 MHz at our 250 MHz board clock: 0.7 % below
 * the 25.175 MHz standard, but within most monitors' tolerance, and it keeps
 * the audio exactly as verified in Plan C2.
 * Scanout is a zero-IRQ DMA pair, so this registers no DMA_IRQ_0 handler. */
#define HAL_DVI_W 480
#define HAL_DVI_H 240
static bool s_dvi;

void hal_init(void) {
    uartkbd_init();
    ws2812_init(pio1, (uint)pio_claim_unused_sm(pio1, true), PIN_LED_DATA);
    ws2812_set_brightness(40);
    ws2812_clear();
    ws2812_show();
    bl_pwm_init();       /* backlight full-on; auto-dim is Plan C1 */
    s_light = opt4001_init();

    /* Audio: codec regs over I2C1, then MCLK + PIO0 I2S. Playback only -- we do
       NOT call audio_capture_start(), so wilidoro registers no DMA_IRQ_0 handler.
       Park the speaker stage powered-down; the first hal_tone() wakes it. */
    codec_nau88c10_init();
    s_audio_ok = codec_nau88c10_input_ok();     /* reg 0x3F rev != 0 => part is alive */
    DIAG("audio: codec %s\n", s_audio_ok ? "ok" : "ABSENT (tones disabled)");
    {   /* mirror the driver's own integer-divider math */
        uint32_t sys = clock_get_hz(clk_sys);
        uint32_t ticks = sys / (256u * 16000u);
        if (ticks) s_audio_fs_hz = sys / (ticks * 256u);
        DIAG("audio: fs=%u Hz (clk_sys=%u kHz)\n",
             (unsigned)s_audio_fs_hz, (unsigned)(sys / 1000u));
    }
    audio_i2s_duplex_init(16000);               /* nominal; real fs is s_audio_fs_hz */
    codec_nau88c10_speaker_low_power();          /* park the output stage down */
    s_audio_awake = false;

    hstx_dvi_init(HAL_DVI_W, HAL_DVI_H);
    s_dvi = (hstx_dvi_video_base() != NULL) &&
            hstx_dvi_video_w() == HAL_DVI_W && hstx_dvi_video_h() == HAL_DVI_H;
    DIAG("dvi: %s (%dx%d stride=%d)\n", s_dvi ? "ok" : "UNAVAILABLE",
         hstx_dvi_video_w(), hstx_dvi_video_h(), hstx_dvi_video_stride());
}

void hal_pump(void) { uartkbd_task(); audio_pump(hal_now_ms()); }

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

void hal_tone(uint16_t hz, uint16_t ms, uint8_t amp) {
    if (!s_audio_ok || ms == 0) return;
    audio_i2s_duplex_play_stop();       /* always stop before re-arming the ring */
    if (hz != 0 && amp != 0) tone_arm(hz, amp);
    /* hz == 0 (a rest) leaves the DAC parked at silence for `ms`. */
    s_tone_end_ms  = hal_now_ms() + ms;
    s_tone_pending = true;
    s_idle_pending = false;
}

void hal_audio_idle(void) {
    audio_i2s_duplex_play_stop();
    s_tone_pending = false;
    s_idle_pending = false;
    audio_sleep();
}

void hal_backlight(uint8_t pct) { bl_pwm_set(pct); }

bool hal_imu(float *ax, float *ay, float *az) { (void)ax;(void)ay;(void)az; return false; }  /* Plan C */
bool hal_lux(float *lux) { return s_light && opt4001_read(lux); }

void hal_beacon_tx(const uint8_t wire[BEACON_WIRE_LEN]) { (void)wire; }                       /* Plan C */
bool hal_beacon_rx(uint8_t wire[BEACON_WIRE_LEN]) { (void)wire; return false; }               /* Plan C */

bool hal_dvi_surface(hal_dvi_surface_t *s) {
    if (!s_dvi) return false;
    s->base   = hstx_dvi_video_base();
    s->stride = hstx_dvi_video_stride();
    s->w      = hstx_dvi_video_w();
    s->h      = hstx_dvi_video_h();
    return true;
}

void hal_dvi_enable(bool on) { if (s_dvi) hstx_dvi_enable(on); }

hal_caps_t hal_caps(void) {
    hal_caps_t c = { .radio=false,.imu=false,.light=s_light,.audio=s_audio_ok,
                     .buttons=true,.leds=true,.dvi=s_dvi };
    return c;
}
