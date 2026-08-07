#include "app.h"
#include "hal.h"
#include "sound.h"
#include "ui.h"
#include "screen_timer.h"
#include "screen_settings.h"
#include "screen_nearby.h"
#include "dimming.h"

/* Ambient auto-dim is useful for a desk timer, but it makes the public demo
 * look like a rail brownout on a dark bench. Keep full brightness by default;
 * products that want adaptive brightness can opt in at compile time. */
#ifndef WILIDORO_AUTO_DIM
#define WILIDORO_AUTO_DIM 0
#endif
#include "led_pattern.h"
#include "timer_view.h"
#if !defined(WILIDORO_BOARD_OG)
#include "dvi_view.h"
#endif
#include "tilt.h"
#include "lvgl.h"

static app_t s_app;
app_t *app(void) { return &s_app; }

/* WS2812s are extremely bright; cap the LED strip well below the panel-derived
   brightness (auto-dim still scales it down further in a dark room). 0..255. */
#define LED_BRIGHT_MAX 40

static lv_obj_t *s_scr[3];
static dim_state_t s_dim;
static tilt_state_t s_tilt;
static uint32_t s_next_lux;
static uint32_t s_next_beacon_ms;   /* next beacon transmit deadline */
static uint32_t s_next_tick_ms;    /* next focus tick (0 = none scheduled) */
static uint32_t s_alarm_next_ms;   /* next alarm re-ring while un-dismissed */
#if !defined(WILIDORO_BOARD_OG)
static dvi_dirty_t s_dvi_dirty;
#endif

/* Re-ring the un-acknowledged focus-end alarm on this cadence (the spec calls
   for ring-until-acknowledged; each ring is a one-shot sequence). */
#define ALARM_REPEAT_MS 5000u
#define FOCUS_TICK_MS   60000u

/* Neighbour entries expire after NEIGHBOR_TTL_MS (60 s), so transmitting every
   ~20 s gives a listener three chances before it drops us. */
#define BEACON_TX_MS 20000u

/* Spread transmits so two co-located units do not lock into permanent collision:
   with a fixed 20 s period their 136 ms bursts can overlap for ~an hour before
   crystal drift separates them. `now` differs per device, so its low bits are a
   good enough jitter source; this needs decorrelation, not cryptographic
   randomness. */
#define BEACON_TX_JITTER_MS 3000u

void app_sound(sound_id_t id) {
    sound_play(&s_app.sound, s_app.settings.theme, id, s_app.settings.volume, hal_now_ms());
}

void app_sound_stop(void) {
    sound_reset(&s_app.sound);
    hal_audio_idle();
}

/* 20 ms cadence: note durations are 40-400 ms, far finer than the 200 ms app tick.
   The while loop dispatches at most one note per callback: sound_next() sets its
   next_ms from the same `now` passed in, and every table note is >=40 ms, so the
   wrap-safe time check always fails on a second call in the same tick. A delayed
   callback (a long LVGL flush, say) just inserts extra silence between notes --
   the tone itself is still stopped on time by hal_pump()'s own deadline. Idle
   power-down after a sequence is likewise left to hal_pump() -> audio_pump(),
   which powers the stage down AUDIO_IDLE_MS after the last tone ends; forcing it
   here would defeat that hysteresis on every softkey blip. */
static void sound_cb(lv_timer_t *t) {
    (void)t;
    uint32_t now = hal_now_ms();
    sound_note_t n;
    while (sound_next(&s_app.sound, now, &n)) hal_tone(n.hz, n.ms, n.amp);
}

/* Blank/unblank the DVI output and force a repaint next tick when re-enabled. */
void app_dvi_apply(void) {
    hal_dvi_enable(s_app.settings.dvi_on);
#if !defined(WILIDORO_BOARD_OG)
    if (s_app.settings.dvi_on) dvi_dirty_reset(&s_dvi_dirty);
#endif
}

/* Re-prime the gate so enabling the feature adopts the board's current
   orientation instead of reporting it as a fresh transition. Called both when
   the toggle flips and when the Default softkey rewrites the settings struct. */
void app_tilt_apply(void) { tilt_init(&s_tilt); }

/* The single place a pm_config_t is built from app()->settings; both the
   settings-save path and Skip's pending-config adoption call through here so
   the field list never has to be kept in sync in two places. */
void app_apply_settings_to_pomo(void) {
    app_settings_t *s = &s_app.settings;
    pm_config_t cfg = { .focus_min = s->focus_min, .short_min = s->short_min,
                        .long_min = s->long_min, .long_every = s->long_every };
    pomodoro_set_config(&s_app.pomo, cfg);
}

static void route_softkey(int col) {
    app_sound(SND_BLIP);
    switch (s_app.screen) {
        case SCREEN_TIMER:    screen_timer_softkey(col);    break;
        case SCREEN_SETTINGS: screen_settings_softkey(col); break;
        case SCREEN_NEARBY:   screen_nearby_softkey(col);   break;
    }
}

/* 100 ms cadence, and the sole owner of I2C1 sensor reads: the OPT4001, the
   BMI323 and the NAU88C10's control registers all share that bus, so one poller
   keeps its traffic predictable instead of two independent ones interleaving.
   The IMU is read every call -- 6 samples per TILT_HOLD_MS window, ~0.35 ms of
   bus time each, so ~0.35 % duty -- and the light sensor every fifth. With
   tilt_pause off the IMU is not touched at all. */
static void sensor_cb(lv_timer_t *t) {
    (void)t;
    uint32_t now = hal_now_ms();

    float ax, ay, az;
    if (s_app.settings.tilt_pause && hal_imu(&ax, &ay, &az)) {
        /* Edge-triggered deliberately: acting on the transition rather than on
           the standing orientation is what stops a manual Pause taken while the
           board is flat from being instantly undone. Breaks are never gated --
           you are meant to walk away from those -- and idle is never started.
           pomodoro_pause/resume are already guarded no-ops outside their valid
           states; these state checks are what confine the rule to focus. */
        switch (tilt_feed(&s_tilt, ax, ay, az, now)) {
            case TILT_EV_LIFTED:
                if (tilt_gate_pauses(s_app.pomo.state)) {
                    pomodoro_pause(&s_app.pomo, now);
                    app_sound(SND_BLIP);
                }
                break;
            case TILT_EV_FLAT:
                if (tilt_gate_resumes(s_app.pomo.state, s_app.pomo.resume_state)) {
                    pomodoro_resume(&s_app.pomo, now);
                    app_sound(SND_BLIP);
                }
                break;
            case TILT_EV_NONE:
                break;
        }
    }

    /* Beacon receive lives here rather than in tick_cb because the capture ring
       fills at up to ~2000 edges/s during a burst; at 200 ms it could overrun and
       lose the middle of a frame. Transmit stays on the slower tick -- a 20 s
       period does not need 100 ms granularity. */
    uint8_t wire[BEACON_WIRE_LEN];
    if (hal_beacon_rx(wire)) {
        beacon_msg_t m;
        if (beacon_unpack(wire, &m)) neighbor_upsert(&s_app.neighbors, &m, now);
    }

    /* auto-dim: poll lux at ~2 Hz through the core dimming curve */
#if WILIDORO_AUTO_DIM
    if ((int32_t)(now - s_next_lux) >= 0) {
        float lux;
        if (hal_lux(&lux)) {
            uint8_t pct = dim_apply(&s_dim, lux);
            hal_backlight(pct);
            hal_led_brightness((uint8_t)((uint32_t)dim_led_brightness(pct) * LED_BRIGHT_MAX / 255));
        }
        s_next_lux = now + 500;
    }
#endif
}

static void tick_cb(lv_timer_t *t) {
    (void)t;
    uint32_t now = hal_now_ms();

    /* advance the pomodoro; surface focus-end as an alarm state */
    pm_event_t ev = pomodoro_tick(&s_app.pomo, now);
    if (ev == PM_EV_FOCUS_ENDED) {
        s_app.alarm_active = true;
        app_sound(SND_FOCUS_END);
        s_alarm_next_ms = now + ALARM_REPEAT_MS;
        s_next_tick_ms = 0;
    }
    if (ev == PM_EV_BREAK_ENDED) app_sound(SND_BREAK_END);

    /* ring until acknowledged */
    if (s_app.alarm_active && (int32_t)(now - s_alarm_next_ms) >= 0) {
        app_sound(SND_FOCUS_END);
        s_alarm_next_ms = now + ALARM_REPEAT_MS;
    }

    /* optional quiet focus tick, once a minute while focus actually runs */
    if (s_app.settings.focus_tick && s_app.pomo.state == PM_FOCUS && !s_app.alarm_active) {
        if (s_next_tick_ms == 0) s_next_tick_ms = now + FOCUS_TICK_MS;
        else if ((int32_t)(now - s_next_tick_ms) >= 0) {
            app_sound(SND_TICK);
            s_next_tick_ms = now + FOCUS_TICK_MS;
        }
    } else {
        s_next_tick_ms = 0;
    }

    /* physical buttons -> softkey columns (grey..red == cols 0..4) */
    hal_btn_t b;
    while (hal_next_button(&b)) {
        if (b <= HAL_BTN_RED) route_softkey((int)b);
        else if (b == HAL_BTN_CANCEL || b == HAL_BTN_HOME) app_goto(SCREEN_TIMER);
    }

    neighbor_expire(&s_app.neighbors, now);

    /* Beacon transmit. A frame is ~136 ms of blocking GPIO toggling, so never
       start one while a chime is sounding: the stall would stretch the tone
       audibly, which is the only genuinely bad symptom. The countdown only
       updates once a second, so the visual hitch is near-invisible. */
    if (s_app.settings.beacon_on && (int32_t)(now - s_next_beacon_ms) >= 0) {
        if (!sound_active(&s_app.sound)) {
            beacon_msg_t out;
            app_beacon_msg(&s_app.settings, &s_app.pomo, now, &out);
            uint8_t tx[BEACON_WIRE_LEN];
            beacon_pack(&out, tx);
            hal_beacon_tx(tx);
            s_next_beacon_ms = now + BEACON_TX_MS - (BEACON_TX_JITTER_MS / 2u)
                             + (now % BEACON_TX_JITTER_MS);
        }
        /* Sound playing: leave the deadline expired and retry on the next tick. */
    }

    /* per-theme LED pattern */
    timer_view_t lv = timer_view_make(&s_app.pomo, s_app.alarm_active, now);
#if !defined(WILIDORO_BOARD_OG)
    /* Big-room DVI display: repaint only when the visible content changes. The
       countdown ticks once a second, so the 200 ms cadence is ample. */
    hal_dvi_surface_t ds;
    if (s_app.settings.dvi_on && hal_dvi_surface(&ds)) {
        if (dvi_view_dirty(&s_dvi_dirty, s_app.settings.theme, &lv)) {
            dvi_surface_t vs = { ds.base, ds.stride, ds.w, ds.h };
            dvi_view_render(s_app.settings.theme, &lv, &vs);
        }
    }
#endif
    /* hal_power_armed(): while a hardware power-off countdown is running, the
       BSP paints that countdown on the LED bar itself, so the app must not
       write LEDs over it (see hal.h). Always false on FW2 and the sim. On
       the OG this now genuinely matters: ws2812_init() runs at boot
       (src/target_og/main.c), so the LED bar is real hardware, not dark.
       hal_og.c holds hal_power_armed() false for about the first 2 s of a
       red-button hold (FWOG_POWER_BAR_DELAY_MS in hal_og.c) even though the
       BSP's own countdown starts painting the instant red goes down, so
       this block keeps repainting the app's own LED pattern through that
       initial ~2 s window rather than ceding the bar to the BSP on the very
       first press -- see FWOG_POWER_BAR_DELAY_MS's comment in hal_og.c for
       the full story, including how hal_pump() closes the sub-threshold
       race on its own side of this seam. */
    if (!hal_power_armed()) {
        led_rgb_t leds[LED_COUNT_MAX];
        const int nled = hal_led_count();
        led_pattern_render(s_app.settings.theme, &lv, leds, nled);
        for (int i = 0; i < nled; i++) hal_led_set(i, leds[i].r, leds[i].g, leds[i].b);
        hal_led_show();
    }

    /* refresh the active screen */
    switch (s_app.screen) {
        case SCREEN_TIMER:    screen_timer_update();    break;
        case SCREEN_SETTINGS: screen_settings_update(); break;
        case SCREEN_NEARBY:   screen_nearby_update();   break;
    }
}

void app_goto(app_screen_t s) {
    s_app.screen = s;
    lv_screen_load_anim(s_scr[s], LV_SCR_LOAD_ANIM_FADE_IN, 150, 0, false);
}

void app_init(void) {
    app_settings_defaults(&s_app.settings);
    pm_config_t cfg = { .focus_min = s_app.settings.focus_min, .short_min = s_app.settings.short_min,
                        .long_min = s_app.settings.long_min, .long_every = s_app.settings.long_every };
    pomodoro_init(&s_app.pomo, cfg);
    neighbor_table_init(&s_app.neighbors);
    dim_init(&s_dim, 100.0f);
    tilt_init(&s_tilt);
#if !defined(WILIDORO_BOARD_OG)
    dvi_dirty_reset(&s_dvi_dirty);
#endif
    s_next_lux = 0;
    s_next_beacon_ms = 0;
    s_app.screen = SCREEN_TIMER; s_app.alarm_active = false;
    sound_reset(&s_app.sound);
    s_next_tick_ms = 0; s_alarm_next_ms = 0;

    s_scr[SCREEN_TIMER]    = screen_timer_create();
    s_scr[SCREEN_SETTINGS] = screen_settings_create();
    s_scr[SCREEN_NEARBY]   = screen_nearby_create();
    lv_screen_load(s_scr[SCREEN_TIMER]);

    lv_timer_create(tick_cb, 200, NULL);
    lv_timer_create(sound_cb, 20, NULL);
    lv_timer_create(sensor_cb, 100, NULL);

    app_dvi_apply();
}
