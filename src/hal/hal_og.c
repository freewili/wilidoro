/* FreeWili OG display-CPU HAL backend.
 *
 * The OG's five coloured buttons map 1:1 onto the softkey columns the app
 * already routes (grey..red == cols 0..4). The FreeWili 2's D-pad, HOME, OK,
 * CANCEL and PAGE have no counterpart and are never emitted; the screens
 * spend a softkey column on Back instead. */
#include "hal.h"
#include "fwog_display.h"
#include "wilidoro_link.h"
#include "common/link/link_uart.h"
#include "common/link/link_frame.h"
#include "pico/stdlib.h"
#include "tone_synth.h"
#include "power/ship_mode.h"   /* FWOG_SHIP_HOLD_MS, for FWOG_POWER_BAR_DELAY_MS's threshold */
#include <string.h>

static void audio_keep_silent(void);   /* defined in the audio section below */

#define BTN_QUEUE_LEN 8
static hal_btn_t s_queue[BTN_QUEUE_LEN];
static uint8_t   s_head, s_tail;
static bool      s_power_armed;
static bool      s_imu_ok;   /* set by hal_init(); see hal_caps() below */

/* ---- Beacon state (Plan OG-D). Declared here rather than beside
   hal_beacon_tx/rx at the foot of the file because hal_init() below brings the
   link up and hal_caps() reads the latched status. See that section for what
   each of these means. ----

   fwog_link_rx_t embeds a 4160-byte payload buffer, so s_link_rx is 4168 bytes
   -- the single largest allocation Plan OG-D adds to this CPU. Against the
   Plan OG-A baseline of 152,188 B of 264 KB that is 1.6 %. */
static fwog_link_rx_t s_link_rx;
static bool           s_link_up;

static uint8_t  s_last_tx[BEACON_WIRE_LEN];
static bool     s_have_tx;
static uint32_t s_last_tx_ms;
static bool     s_show_self;          /* Nearby's Self softkey; default hidden */

static wd_link_status_t s_status;
static bool             s_status_valid;
static uint32_t         s_status_ms;

/* Bytes drained per hal_beacon_rx() call. app_poll() calls it every 100 ms, so
   this is several whole frames' worth against a beacon that repeats every 20 s
   -- and short enough that a saturated or babbling link cannot starve the LVGL
   tick. */
#define WD_DRAIN_BUDGET 256u

/* How long a 0x42 stays believed. Main resends every 5 s, so this tolerates
   two consecutive losses before hal_caps().radio goes false. */
#define WD_STATUS_STALE_MS 15000u

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

/* UX delay before the shutdown countdown is allowed to take over the LED bar.
 * fwog_power_poll() (wiliOGbsp, read-only) starts painting the moment red
 * goes down, which board feedback called too eager -- the ask is nothing
 * visible until the hold has been sustained for a full 2 s. wiliOGbsp cannot
 * be changed to add that delay itself, so it is applied here, on our side of
 * the hal_power_armed() seam: hal_pump() below only lets s_power_armed go
 * true once fwog_power_t.progress (0..100 over FWOG_SHIP_HOLD_MS) has passed
 * this threshold. Below the threshold s_power_armed stays false, so app.c's
 * `if (!hal_power_armed())` block (called from tick_cb, an LVGL timer on a
 * 200 ms period -- see app_init()) keeps repainting the app's own LED pattern
 * unconditionally, not just when it changes, and hal_pump() (called every
 * raw main-loop iteration, well under 200 ms) runs before that repaint in
 * the same loop -- see src/target_og/main.c / src/sim/main.c -- so the app's
 * colours are the last thing written on any iteration where both happen to
 * run together. That alone would only be a mitigation, not a guarantee:
 * BSP's own ship_render() also writes straight to the WS2812 driver, on its
 * own 50 ms cadence starting immediately at press (FWOG_SHIP_RENDER_MS in
 * wiliOGbsp/bsp/display_cpu/power/power_poll.c), independent of this
 * threshold -- so between the app's 200 ms repaints a low-progress BSP frame
 * could reach the physical strip and stay visible for up to ~200 ms before
 * the app's next tick overwrote it. hal_pump() below closes that gap itself:
 * while p.armed is true but armed_now (post-threshold) is not, it re-pushes
 * this HAL's own cached LED colours (hal_led_show(), which has no
 * dependency on the app) on every hal_pump() call -- roughly every 2 ms, not
 * every 200 ms -- so any sub-threshold BSP frame that lands in the gap is
 * out-written on the very next main-loop iteration instead of surviving
 * until app.c's next repaint. That shrinks the exposure window from ~200 ms
 * to ~one loop iteration (~2 ms), and only while red is physically held
 * pre-threshold. */
#define FWOG_POWER_BAR_DELAY_MS 2000u

/* progress/100 of the hold time must reach FWOG_POWER_BAR_DELAY_MS before the
 * countdown is allowed to show. Rearranged to avoid both integer division
 * (progress is coarse: only 0..100) and floating point (none allowed here):
 *   progress/100 * FWOG_SHIP_HOLD_MS >= FWOG_POWER_BAR_DELAY_MS
 *   progress * FWOG_SHIP_HOLD_MS     >= FWOG_POWER_BAR_DELAY_MS * 100
 * Both sides fit comfortably in uint32_t: progress<=100, FWOG_SHIP_HOLD_MS is
 * 6000, so the left side maxes out at 600000; the right side is a compile-time
 * constant, 200000. */
static bool power_bar_delay_elapsed(unsigned progress) {
    return (uint32_t)progress * FWOG_SHIP_HOLD_MS >= FWOG_POWER_BAR_DELAY_MS * 100u;
}

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

    /* +/-2 g: tilt-to-pause cares about the direction of the 1 g gravity
       vector, and the narrowest range gives the finest resolution for it.
       Owning this call here (not in target_og/main.c) is what lets
       hal_caps().imu below report the truth: s_imu_ok is exactly
       lis3dh_configure()'s own success/failure, the same signal
       target_og/main.c's heartbeat now reads back out through hal_caps()
       instead of keeping a second, HAL-external copy of it. On failure
       lis3dh_configure() itself DIAGs (whoami mismatch or I2C fault) and
       leaves the part at its power-on power-down default; hal_imu() below
       is written to report false forever in that case, not garbage. */
    lis3dh_init();
    s_imu_ok = lis3dh_configure(LIS3DH_RANGE_2G);

    /* The display BOOTLOADER deinits the link before jumping here (bl_jump.c),
       so the application must bring it back up itself -- board_init() does not.
       apps/bench_display/main.c:981 is the reference for these two lines. */
    s_link_up = fwog_link_uart_init(FWOG_LINK_BAUD);
    fwog_link_rx_init(&s_link_rx);
    s_show_self = false;
    if (!s_link_up) DIAG("[wilidoro] link uart init FAILED -- no beacon\n");
}

/* Once per main-loop iteration. fwog_power_poll() is THE button read: calling
   fwog_buttons_poll() again here would consume edges out from under the
   ship-mode hold machine, which depends on that debounce state. */
void hal_pump(void) {
    const fwog_power_t p = fwog_power_poll(hal_now_ms());
    /* p.armed goes true the instant red is pressed; delay handing the LED bar
       to the BSP until the hold has cleared FWOG_POWER_BAR_DELAY_MS, per the
       comment on that constant above. */
    const bool armed_now = p.armed && power_bar_delay_elapsed(p.progress);
    if (p.armed && !armed_now) {
        /* Pre-threshold hold: fwog_power_poll() above may have just let the
           BSP's own ship_render() paint a sub-threshold countdown frame
           straight onto the WS2812 strip (see FWOG_POWER_BAR_DELAY_MS's
           comment). Re-push our own cached colours right now, every
           hal_pump() call, rather than waiting for app.c's next 200 ms
           repaint -- see that comment for why this closes the gap down to
           about one loop iteration instead of leaving it open for up to
           200 ms. */
        hal_led_show();
    }
    s_power_armed = armed_now;

    /* fwog_btn_id_t is GRAY,YELLOW,GREEN,BLUE,RED == 0..4, and hal_btn_t is
       GREY,YELLOW,GREEN,BLUE,RED == 0..4. Same order, same colours. */
    for (unsigned i = 0; i < FWOG_BTN_COUNT; i++) {
        if (p.buttons.pressed & (1u << i)) queue_push((hal_btn_t)i);
    }

    i2s_audio_process();   /* feeds the A/B chain; starves and truncates without it */
    audio_keep_silent();   /* fix round 1: the idle bus is audible without this */
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

/* A block of real silence, played on a loop whenever no note is sounding.
   i2s_audio_start() always transfers a full I2S_AUDIO_BUFF_SIZE (1024
   sample) buffer -- i2s_audio_fill_buffer() zero-pads whatever is left after
   a shorter source is consumed -- so this 512-sample source arms one
   1024-sample = 128 ms transfer at the driver's 8 kHz rate, and
   audio_keep_silent() re-arms it about 8 times a second. Every sample is
   zero, so re-arming is inaudible. */
#define SILENCE_SAMPLES 512u
static const int16_t s_silence_buf[SILENCE_SAMPLES];   /* .rodata, all zero */

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

/* Fix round 1 -- board verification found the chime itself correct but
   audible noise after it ends. Root cause, confirmed from
   wiliOGbsp/bsp/display_cpu/audio/i2s_audio.h's "Trap 2": IC17 (MAX98357A)
   has SD_MODE hard-pulled to 3V3 with no GPIO wired to it, so there is no
   mute/shutdown line on this board -- "AN IDLE I2S BUS IS AN AUDIBLE STATE,
   not an off state. Whatever the PIO shift register last held keeps looping
   out at the bit clock rate for as long as the state machine runs, even with
   no DMA feeding it." i2s_audio_stop()/the natural end-of-note path
   (i2s_audio.c's stop_internal()) push exactly ONE best-effort zero word
   before returning to idle -- the header calls that "BEST EFFORT, not a real
   mute", not a guarantee. pio_sm_set_enabled() is never called with false
   anywhere in that driver, so the state machine keeps running and keeps
   shifting whatever is left in its pipeline once that one zero word has
   gone through.
   The fix belongs here, not in the read-only wiliOGbsp submodule: whenever
   i2s_audio_is_idle() reports true, re-arm a block of `const` (all-zero)
   samples through the driver's normal i2s_audio_start() entry point -- the
   same call hal_tone() uses for a real note, just fed silence instead. That
   keeps the SM running continuously through DMA the whole time, which is
   what removes both the rate race and the frame-phase problem the two
   rejected approaches below hit: there is no separate top-up path racing the
   DMA's own consumption rate, and the SM is never stopped or parked, so
   there is no re-entry alignment to get wrong. hal_pump() calls
   audio_keep_silent() every main-loop iteration (~2 ms), including before
   the first tone ever plays, so the bus is silenced from shortly after
   i2s_audio_init() at boot, not only after the first note ends. After a real
   note ends, up to one loop iteration (~2 ms) can pass before this re-arms,
   during which the SM re-shifts the last sample already in its pipeline --
   ear-confirmed inaudible on hardware. */
static void audio_keep_silent(void) {
    if (!i2s_audio_is_idle()) return;
    /* Re-arm the silence loop. Two earlier approaches were measured and
       rejected on this board, both worth recording so neither is retried:

       1. Topping up the PIO TX FIFO with zero words from here. It silences the
          bus from boot, but cannot silence it after a note: the SM consumes
          8000 words/s and this loop, running every ~2 ms, supplies about 2000.
          Between top-ups the SM re-shifts whatever it last held -- zeros from
          boot (silent), audio samples after a chime (noise). That asymmetry is
          exactly what was observed on hardware.

       2. Parking the SM (pio_sm_set_enabled false) while idle. That did kill
          the noise completely, but the chime then played correctly only
          sometimes: pio_sm_restart() resets the shift counters and clkdiv
          phase but NOT the program counter, so a SM parked mid-frame resumes
          mid-frame and the next note comes out misaligned. The driver owns the
          program offset, so a clean re-entry point is not reachable from here.

       Letting the DMA play real zeros keeps the SM running continuously, which
       removes both problems at once -- no rate race, and no frame phase to get
       wrong. It costs one 128 ms re-arm about 8 times a second and 16 KB/s of
       DMA bandwidth. */
    (void)i2s_audio_start(s_silence_buf, SILENCE_SAMPLES, true, false);
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
/* LIS3DH at +/-2 g. lis3dh_raw_to_mg() is the BSP's own host-tested
   conversion (wiliOGbsp/bsp/display_cpu/sensors/lis3dh.h:204-214): it undoes
   the register pair's left-justification with a sign-preserving >>6 before
   scaling by the range's mg/digit figure, so it is correct for any range
   without a hand-derived counts-per-g constant here. */

/* WHICH AXIS IS "FLAT" IS BENCH-MEASURED, NOT DERIVABLE FROM THE DATASHEET.
   Measured 2026-07-29 via the (now-removed) temporary DIAG in
   src/target_og/main.c, reading milli-g in three positions:

     position                          x      y      z    (mg)
     1. flat on desk, screen up       ~0    -12   +1064
     2. tipped up ~36 deg from level  -10   -610   +850
     3. vertical, screen facing user  -14  -1027    +33

   Z is the axis that reads +1 g when the board is level and falls toward 0
   as the board is lifted -- that is the flat axis, and its sign is
   POSITIVE. tilt.c computes c = az / |a| and treats c >= TILT_FLAT_COS as
   flat, i.e. it wants az to read +1 g when level, which is exactly what Z
   already does -- so the mapping below is a straight-through pass with no
   negation and no axis swap. (Vector magnitude was ~1.03-1.06 g in all
   three positions, independently confirming lis3dh_raw_to_mg()'s scaling
   regardless of which axis turned out to be flat.)

   The FreeWili 2's BMI323 sits in a different orientation, so this mapping
   does not transfer to that board. This is the ONE place it lives; do not
   "tidy" it (e.g. reorder/negate to "look right") without a board in hand
   to re-confirm against. Full measurement, including angles and the
   hysteresis-band note for position 2, is in docs/hardware-notes.md. */
/* Last GENUINE sample, seeded into `s` below before every poll. static ->
   zero-initialized, so before the first real sample ever arrives the seed
   reads (0,0,0). */
static lis3dh_sample_t s_last_sample;
static bool s_last_valid;

bool hal_imu(float *ax, float *ay, float *az) {
    /* lis3dh_process() (wiliOGbsp/bsp/display_cpu/sensors/lis3dh.c:83-87,
       and the header's own documented contract on lis3dh_advance() /
       lis3dh_process()) has THREE outcomes, not the two an
       `if (!ok) return false;` idiom assumes:
         1. false             -- an I2C read failed this call.
         2. true, no new data -- STATUS's ZYXDA bit was clear. In practice
            this is RARE, not routine: hal_imu() is not on hal_pump()'s
            ~2 ms main-loop cadence at all -- its only caller is sensor_cb()
            in src/app/app.c (~:104), on a 100 ms lv_timer (app.c:98,284),
            ten times SLOWER than the sensor's 10 ms (100 Hz) output, so a
            fresh sample is essentially always waiting by the time this
            polls. The outcome that IS routine is the permanent one: if
            lis3dh_configure() failed to write CTRL_REG1/CTRL_REG4
            (s_imu_ok false, see hal_init()) the part never leaves its
            power-on power-down default and ZYXDA never sets, for the life
            of the boot. Either way out_sample/out_motion are left EXACTLY
            as the caller passed them in -- documented BSP behaviour, not
            an omission.
         3. true, new data    -- out_sample holds a fresh reading.
       Declaring `lis3dh_sample_t s;` uninitialized and treating any `true`
       as "sample valid" (the bug this replaces) converts whatever garbage
       was on the stack to a plausible +/-2 g vector in outcome 2 -- often
       enough that tilt.c's `mag < TILT_MAG_MIN` guard does not reject it,
       so a running session could pause on its own.

       Seeding `s` from the last GENUINE sample instead is exactly what
       outcome 2's "leave untouched" contract calls for: on a "no new data"
       poll, `s` comes back holding the last real reading, not garbage. And
       refusing to report anything until a real sample has actually been
       seen (s_last_valid) closes both the startup window before the first
       ZYXDA-set poll -- at this HAL's 100 ms polling cadence (see outcome 2
       above) that window is effectively zero -- and the permanent
       stuck-in-power-down case, which is not. */
    lis3dh_sample_t s = s_last_sample;
    lis3dh_motion_t m;
    if (!lis3dh_process(LIS3DH_MOVE_THRESHOLD_DEFAULT, &s, &m)) {
        /* Only clears "have we ever seen a genuine sample" -- it does NOT
           invalidate s_last_sample. lis3dh_process() zeroes its own `s`
           local on this path (the BSP's documented read-failure semantics,
           lis3dh.h), but it never touches our static, so s_last_sample
           keeps whatever real reading it held before the glitch. If a
           sample had already arrived, the very next successful poll
           reseeds from that (nonzero) pre-glitch value and republishes it
           as valid -- a stale but real orientation, exactly like the
           ordinary no-new-data path below. This flag only matters
           pre-first-sample, where it correctly keeps hal_imu() returning
           false until a genuine reading arrives. */
        s_last_valid = false;
        return false;
    }
    /* Zero-initialized statics make the pre-first-sample seed (0,0,0);
       gravity alone puts a real reading nowhere near that on every axis at
       once. Worst case is a corner orientation (gravity split evenly
       across x/y/z): each axis still reads ~1/sqrt(3) g =~ 0.577 g, i.e.
       ~144 digits at 4 mg/digit -- ~9200 raw LSB once left-justified
       (digit << 6; see lis3dh_raw_to_mg()), not the digit count itself.
       So "still exactly (0,0,0) and never yet valid" can only mean no
       genuine sample has arrived, never a real reading. */
    if (!s_last_valid && s.x == 0 && s.y == 0 && s.z == 0) return false;
    s_last_sample = s;
    s_last_valid = true;
    *ax = (float)lis3dh_raw_to_mg(s.x, LIS3DH_RANGE_2G) / 1000.0f;
    *ay = (float)lis3dh_raw_to_mg(s.y, LIS3DH_RANGE_2G) / 1000.0f;
    *az = (float)lis3dh_raw_to_mg(s.z, LIS3DH_RANGE_2G) / 1000.0f;
    return true;
}

/* ---- Beacon: Plan OG-D, over the inter-CPU link ----
 *
 * This CPU has no radio. hal_beacon_tx/rx are messages to the main CPU, which
 * owns both CC1101s. Nothing above the HAL knows the difference: beacon_rx.c,
 * the neighbour table and the Nearby screen are untouched. */

void hal_beacon_tx(const uint8_t wire[BEACON_WIRE_LEN]) {
    if (!s_link_up) return;
    memcpy(s_last_tx, wire, BEACON_WIRE_LEN);
    s_last_tx_ms = hal_now_ms();
    s_have_tx = true;
    uint8_t p[sizeof(wd_link_beacon_t)];
    const size_t n = wd_link_build_beacon(p, sizeof p, WD_LINK_MSG_BEACON_TX, wire);
    if (n != 0u) (void)fwog_link_uart_send_frame(p, n);
}

bool hal_beacon_rx(uint8_t wire[BEACON_WIRE_LEN]) {
    if (!s_link_up) return false;
    uint8_t b;
    size_t len = 0;
    for (unsigned i = 0; i < WD_DRAIN_BUDGET && fwog_link_uart_read(&b); i++) {
        if (!fwog_link_rx_byte(&s_link_rx, b, &len)) continue;
        switch (wd_link_type(s_link_rx.buf, len)) {
        case WD_LINK_MSG_STATUS: {
            wd_link_status_t st;
            if (!wd_link_parse_status(s_link_rx.buf, len, &st)) break;
            /* On CHANGE only -- a healthy board must not emit a line every
               5 s. This is the only consumer of the RSSI and LQI fields, and
               it exists so those numbers are reachable when the display's USB
               console is the one plugged in rather than main's. */
            if (!s_status_valid || memcmp(&st, &s_status, sizeof st) != 0) {
                DIAG("[wilidoro] radio cs0=%d cs1=%d selftest=%u rssi=%d lqi=%u crc=%d\n",
                     (st.flags & WD_STATUS_CS0_UP) ? 1 : 0,
                     (st.flags & WD_STATUS_CS1_UP) ? 1 : 0,
                     (unsigned)st.selftest, (int)st.rssi_dbm,
                     (unsigned)(st.lqi & 0x7Fu), (st.lqi & 0x80u) ? 1 : 0);
            }
            s_status = st;
            s_status_valid = true;
            s_status_ms = hal_now_ms();
            break;
        }
        case WD_LINK_MSG_BEACON_RX: {
            uint8_t frame[BEACON_WIRE_LEN];
            if (!wd_link_parse_beacon(s_link_rx.buf, len, frame)) break;
            if (!s_show_self &&
                wd_link_is_echo(s_have_tx, s_last_tx, s_last_tx_ms,
                                hal_now_ms(), frame)) {
                break;                  /* our own echo: drop, keep draining */
            }
            memcpy(wire, frame, BEACON_WIRE_LEN);
            return true;
        }
        default:
            break;                       /* not ours -- the BSP's, or noise */
        }
    }
    return false;
}

void hal_beacon_show_self(bool show) { s_show_self = show; }

hal_caps_t hal_caps(void) {
    hal_caps_t c = {0};   /* light, dvi stay false: no hardware on this board */
    c.buttons = true;
    c.leds    = true;
    c.audio   = true;
    c.imu     = s_imu_ok;   /* true iff lis3dh_configure() actually found the part -- see hal_init() */
    /* True only while main is actually saying so. Requiring BOTH radios is
       deliberate: a dead CS1 means Nearby can never populate, and a dead CS0
       means we are invisible to everyone else -- either way "radio: ok" on the
       Settings screen would be a lie. Goes false on its own if the main CPU
       dies or the link breaks, not only if a radio is absent. */
    c.radio = s_status_valid
           && (uint32_t)(hal_now_ms() - s_status_ms) <= WD_STATUS_STALE_MS
           && (s_status.flags & (WD_STATUS_CS0_UP | WD_STATUS_CS1_UP))
              == (WD_STATUS_CS0_UP | WD_STATUS_CS1_UP);
    return c;
}
