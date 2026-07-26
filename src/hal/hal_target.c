#include "hal.h"
#include "fw2.h"
#include "input/uartkbd.h"
#include "input/uartkbd_parse.h"
#include "leds/ws2812_driver.h"
#include "leds/led_color.h"
#include "bl_pwm.h"
#include "sensors/opt4001.h"
#include "sensors/bmi323.h"
#include "radio/cc1101.h"
#include "radio/ook_tx.h"
#include "radio/gdo_capture.h"
#include "beacon_rx.h"
#include "platform/ioexp.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "platform/diag.h"
#include <string.h>

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
 *    s_audio_fs_hz/TONE_FRAMES ~ 15.6 Hz; src/app/sound.c keeps every note >= 440 Hz
 *    so the resulting detune stays under ~2 %.
 *  - Duration is a deadline serviced by hal_pump(), never a busy-wait: hal_tone
 *    must not block the LVGL main loop.
 *  - s_audio_fs_hz is the REAL rate, computed at runtime in hal_init(): MCLK is
 *    an integer PWM divide of clk_sys, so fs = clk_sys/(ticks*256) -- 16009 Hz
 *    at our 250 MHz board clock (250e6/61 = 4.0984 MHz, 4.0984e6/256 = 16009),
 *    not the nominal 16000. audio_i2s_duplex_init() still takes the nominal
 *    16000 (it derives the divider from it). See wilibsp/docs/hardware/facts.md,
 *    "Audio: lock LRCK to MCLK/256".
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
static bool s_imu;

/* 433.92 MHz ISM. A hardware-routing fact, not app policy, so it lives here
   rather than in core/beacon.h beside the wire format. */
#define BEACON_HZ 433920000u
static bool s_radio;
static beacon_rx_t s_brx;

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
/* The BSP sizes framebuf[] from these at compile time (root CMakeLists.txt sets
   them); a silent drift would leave DVI permanently unavailable at runtime. */
_Static_assert(HAL_DVI_W == HSTX_VID_W_MAX && HAL_DVI_H == HSTX_VID_H_MAX,
               "HAL_DVI_* must match the HSTX_VID_*_MAX compile definitions");
static bool s_dvi;

/* Defined below, next to hal_beacon_tx; forward-declared so hal_init (which
   comes first in the file) can call it at bring-up. */
static void radio_loopback_selftest(void);

/* Put the CC1101 back into async-transparent OOK RX, where GDO0 carries the
   demodulated data edges that gdo_capture timestamps. Both bring-up and the end
   of every transmit return here, so listening is the resting state. */
static void radio_listen(void) {
    cc1101_monitor_rx(BEACON_HZ, CC1101_MOD_ASK_OOK);
}

void hal_init(void) {
    uartkbd_init();
    ws2812_init(pio1, (uint)pio_claim_unused_sm(pio1, true), PIN_LED_DATA);
    ws2812_set_brightness(40);
    ws2812_clear();
    ws2812_show();
    bl_pwm_init();       /* backlight full-on; auto-dim is Plan C1 */
    s_light = opt4001_init();
    /* Same I2C1 bus as the OPT4001 above and the codec's control registers
       below. bmi323_init() DIAGs its own chipid check. */
    s_imu = bmi323_init();

    /* Route a CC1101 antenna before any radio SPI traffic. ioexp_antenna talks
       to the PCAL6524 over I2C1 -- the same bus sensor_cb owns -- so it must stay
       here in init, before any timer exists, and never move into a callback. */
    ioexp_antenna(ANT_CC1101_433);
    s_radio = cc1101_init();          /* DIAGs PARTNUM/VERSION itself */
    DIAG("radio: cc1101 %s\n", s_radio ? "ok" : "ABSENT (beacon disabled)");
    if (s_radio) {
        gdo_capture_init();
        gdo_capture_start();
        radio_listen();
        radio_loopback_selftest();
    }

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

/* Accelerometer only. The BSP burst-reads all six axes in one transaction; the
   gyro is not worth forking the driver to skip, and the tilt gate needs nothing
   but the gravity vector. */
bool hal_imu(float *ax, float *ay, float *az) {
    if (!s_imu) return false;
    bmi323_reading_t r;
    if (!bmi323_read(&r)) return false;
    *ax = r.ax; *ay = r.ay; *az = r.az;
    return true;
}
bool hal_lux(float *lux) { return s_light && opt4001_read(lux); }

/* Chunk size for draining the PIO2 capture ring. Shared by hal_beacon_tx's
   post-transmit flush, the loopback self-test, and hal_beacon_rx. */
#define RX_DRAIN_CHUNK 128u

/* Drain the PIO2 capture ring dry, discarding every word, in a bounded loop
   (so a somehow-still-full ring can't spin this forever). Used wherever
   leftover ring contents must not reach the framer: hal_beacon_tx's
   post-transmit anti-echo drain, and the loopback self-test's pre-transmit
   (stale debris) and post-test (its own LOOPBACK echo) drains. Always called
   as a sibling of beacon_tx_raw() -- before or after it, never nested inside
   its call chain -- see the stack-peak comment on beacon_tx_raw's durs[]. */
static void drain_ring_dry(void) {
    for (int rounds = 0; rounds < 64; rounds++) {
        uint32_t discard[RX_DRAIN_CHUNK];
        if (gdo_capture_drain(discard, RX_DRAIN_CHUNK) < RX_DRAIN_CHUNK) break;
    }
}

/* The actual bit-banging, shared by hal_beacon_tx and the loopback self-test
   below: encode and blast the wire frame out GDO0, then return the radio to
   listening. Deliberately does NOT touch the capture ring or the framer --
   hal_beacon_tx and the self-test want opposite things done with the echo
   this produces (discarded vs. decoded), so that decision lives in the two
   callers, not here. Returns false (nothing sent) if the frame was empty. */
static bool beacon_tx_raw(const uint8_t wire[BEACON_WIRE_LEN]) {
    /* durs[] here (1104 B) plus beacon_ook_encode's own uint8_t hb[272] add up
       to 1376 B on top of whichever function calls beacon_tx_raw(). The worst
       boot-path caller is radio_loopback_selftest: its own round-loop
       durs[RX_DRAIN_CHUNK] (512 B) plus a few small locals (sent/got/m/ints)
       contribute roughly another 570 B, for a worst-case peak around 1.9 KB
       against a .stack_dummy of 2048 B -- worst-case because it assumes GCC
       does NOT share stack slots between that round-loop buffer and anything
       else in the same function. drain_ring_dry()'s own 512 B buffer is NOT
       part of this peak: it is only ever called as a sibling of
       beacon_tx_raw() (before or after it), never nested inside this call
       chain, so the two frames are never concurrently on the stack -- if that
       ever changes, re-derive this figure. Safe only by accident regardless:
       .scratch_x/.scratch_y are both 0 B in this build, so the stack has
       ~8 KB of unused room below it before hitting anything real. Not a
       designed margin -- re-check this if either scratch region ever gets
       used. */
    uint32_t durs[BEACON_MAX_DURS];
    bool start_level = false;
    size_t n = beacon_ook_encode(wire, durs, BEACON_MAX_DURS, &start_level);
    if (n == 0) return false;

    cc1101_tx_ook_start(BEACON_HZ);              /* key the carrier; GDO0 becomes SIO */
    ook_tx_send(durs, (uint32_t)n, start_level);
    /* Re-attach the PIO to GDO0 BEFORE stopping TX: this makes the pad a PIO
       input while the CC1101 is still 3-stated, so the chip's own driver
       re-enables into an already-released pin instead of briefly fighting the
       MCU's SIO drive (the previous order double-drove the pin for one call). */
    gdo_capture_attach_pin();                    /* undo the SIO takeover */
    cc1101_tx_ook_stop();
    radio_listen();
    return true;
}

/* Blocking: a frame is 136 bits x 2 half-bits x 500 us = ~136 ms of GPIO
   toggling. ook_tx_send drives GDO0 (GPIO32) directly and touches no SPI; only
   the short start/stop register bursts do, and those take the shared bus through
   the BSP's own spi_bus arbiter. The caller gates the cadence. */
void hal_beacon_tx(const uint8_t wire[BEACON_WIRE_LEN]) {
    if (!s_radio) return;
    if (!beacon_tx_raw(wire)) return;

    /* PIO2 samples the GDO0 pad no matter who drives it -- the same property
       the loopback self-test below relies on -- so every burst we just sent
       lands right back in the capture ring as if a peer had sent it, and it
       was captured live during the ~136 ms blocking send above (the ring
       fills in real time, independent of anything this function does after).
       Left alone, the next hal_beacon_rx() would decode our own frame and
       neighbor_upsert() would file this device under its own name. Drain the
       echo out before it reaches the framer, and reset the framer itself:
       the burst's edges arrived with no real trailing gap (that is the whole
       F1 problem) and would otherwise corrupt a partially-assembled peer
       segment that was open when we started transmitting. */
    drain_ring_dry();
    beacon_rx_init(&s_brx);
}

/* One-shot self-test, run at bring-up (from hal_init, BEFORE app_init runs --
   so this transmits one real frame at every boot regardless of the app's
   `beacon_on` setting. Harmless today since settings are not persisted across
   boots, but it will be a real bug -- a boot-time transmit the user's saved
   "beacon off" preference cannot suppress -- the moment they are.)
   PIO2 samples the GDO0 pad even while ook_tx_send drives it as an SIO output
   -- wilibsp's hello_cc1101 sends 24 pulses and drains exactly 24 edges -- so
   transmitting our own beacon and draining the capture exercises the whole
   chain: pack -> ook_encode -> ook_tx timing -> PIO2/DMA capture -> framer ->
   ook_decode -> unpack.
   Everything except the RF air path and the CC1101's own demodulator, neither of
   which has ever been demonstrated on this hardware by anyone.
   It fails closed: if the pad-sampling assumption does not hold, the decode
   simply fails and the log says so. It cannot produce a false pass.

   Calls beacon_tx_raw() directly rather than hal_beacon_tx(): this test's
   entire premise is decoding the device's own echo, which is exactly what
   hal_beacon_tx's anti-self-reception drain (see above) exists to discard.
   Going through hal_beacon_tx here would make the drain eat the self-test's
   own burst before this function ever got to look at it, and the test would
   report FAILED unconditionally. */
static void radio_loopback_selftest(void) {
    if (!s_radio) return;
    beacon_msg_t m;
    memcpy(m.name, "LOOPBACK", BEACON_NAME_LEN);
    m.state = BST_FOCUS; m.minutes_left = 42; m.completed = 7;
    uint8_t sent[BEACON_WIRE_LEN];
    beacon_pack(&m, sent);

    beacon_rx_init(&s_brx);

    /* beacon_rx_init only resets the framer struct, not gdo_capture's own
       tail -- drain any pre-existing capture-ring debris now, before we
       transmit, or it prepends to the segment under test and can corrupt it. */
    drain_ring_dry();

    beacon_tx_raw(sent);                     /* also returns the radio to listening */

    /* A gap first, so the framer knows the following run is high. */
    uint8_t got[BEACON_WIRE_LEN];
    bool ok = false;
    (void)beacon_rx_push(&s_brx, BEACON_GAP_US * 4u, got);
    for (int round = 0; round < 8 && !ok; round++) {
        uint32_t durs[RX_DRAIN_CHUNK];
        uint32_t n = gdo_capture_drain(durs, RX_DRAIN_CHUNK);
        for (uint32_t i = 0; i < n && !ok; i++)
            if (beacon_rx_push(&s_brx, durs[i], got)) ok = true;
        if (!ok) sleep_ms(5);
    }
    /* The burst's trailing low run never gets a natural closing gap (F1) --
       flush once the drain rounds above are done to close whatever segment is
       still open. By this point beacon_tx_raw has long since returned (it
       blocks for the whole ~136 ms burst), so every edge is already sitting
       in the ring; the flush is what actually closes the frame. */
    if (!ok) ok = beacon_rx_flush(&s_brx, got);
    if (ok) ok = (memcmp(sent, got, BEACON_WIRE_LEN) == 0);
    DIAG("beacon: loopback %s\n", ok ? "ok" : "FAILED");

    beacon_rx_init(&s_brx);                  /* discard self-test state */
    /* And drain the ring dry: this test's own burst is otherwise still
       sitting there and would later be decoded by hal_beacon_rx() as a
       "LOOPBACK" neighbour -- the same self-reception bug F2 fixes for
       normal operation, just via this test instead. */
    drain_ring_dry();
}

/* Drain the PIO2 capture ring fully and feed the framer. The loop matters: during
   a burst the line carries up to ~2000 edges/s, so a single fixed-size drain can
   fall behind and lose the middle of a frame. Draining until a short read means
   the chunk size stops mattering.

   Push the WHOLE drained chunk before returning, keeping only the FIRST decoded
   frame rather than returning as soon as one decodes: the run that closes frame
   N can land in the same 100 ms poll as the runs that start frame N+1 (a frame
   takes ~136 ms to transmit, but the tail of one and the head of the next
   sharing a poll is routine), so an early return would systematically discard
   N+1's leading edges. Do not add a pending-duration queue for this -- pushing
   everything and remembering only the first hit is enough.

   If the whole poll drains zero edges and a segment is still open, the burst is
   provably over (zero edges across 100 ms is three orders of magnitude past the
   ~1 ms longest legal run) and the line has gone quiet with no natural gap
   coming: flush it. Do NOT flush after a partial drain -- that would try_decode
   a frame that is still arriving, fail, and destroy it. */
bool hal_beacon_rx(uint8_t wire[BEACON_WIRE_LEN]) {
    if (!s_radio) return false;
    uint32_t durs[RX_DRAIN_CHUNK];
    uint8_t frame[BEACON_WIRE_LEN];
    bool got = false;
    uint32_t total = 0;
    for (;;) {
        uint32_t n = gdo_capture_drain(durs, RX_DRAIN_CHUNK);
        total += n;
        for (uint32_t i = 0; i < n; i++)
            if (beacon_rx_push(&s_brx, durs[i], frame) && !got) {
                memcpy(wire, frame, BEACON_WIRE_LEN);
                got = true;
            }
        if (n < RX_DRAIN_CHUNK) break;   /* ring is empty */
    }
    if (total == 0) {
        if (beacon_rx_flush(&s_brx, frame) && !got) {
            memcpy(wire, frame, BEACON_WIRE_LEN);
            got = true;
        }
    }
    return got;
}

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
    hal_caps_t c = { .radio=s_radio,.imu=s_imu,.light=s_light,.audio=s_audio_ok,
                     .buttons=true,.leds=true,.dvi=s_dvi };
    return c;
}
