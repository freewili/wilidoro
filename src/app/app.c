#include "app.h"
#include "hal.h"
#include "sound.h"
#include "ui.h"
#include "screen_timer.h"
#include "screen_settings.h"
#include "screen_nearby.h"
#include "dimming.h"
#include "led_pattern.h"
#include "timer_view.h"
#include "lvgl.h"

static app_t s_app;
app_t *app(void) { return &s_app; }

/* WS2812s are extremely bright; cap the LED strip well below the panel-derived
   brightness (auto-dim still scales it down further in a dark room). 0..255. */
#define LED_BRIGHT_MAX 40

static lv_obj_t *s_scr[3];
static dim_state_t s_dim;
static uint32_t s_next_lux;
static uint32_t s_next_tick_ms;    /* next focus tick (0 = none scheduled) */
static uint32_t s_alarm_next_ms;   /* next alarm re-ring while un-dismissed */

/* Re-ring the un-acknowledged focus-end alarm on this cadence (the spec calls
   for ring-until-acknowledged; each ring is a one-shot sequence). */
#define ALARM_REPEAT_MS 5000u
#define FOCUS_TICK_MS   60000u

void app_sound(sound_id_t id) {
    sound_play(&s_app.sound, s_app.settings.theme, id, s_app.settings.volume, hal_now_ms());
}

void app_sound_stop(void) {
    sound_reset(&s_app.sound);
    hal_audio_idle();
}

/* 20 ms cadence: note durations are 40-400 ms, far finer than the 200 ms app tick. */
static void sound_cb(lv_timer_t *t) {
    (void)t;
    uint32_t now = hal_now_ms();
    bool was_active = sound_active(&s_app.sound);
    sound_note_t n;
    while (sound_next(&s_app.sound, now, &n)) hal_tone(n.hz, n.ms, n.amp);
    if (was_active && !sound_active(&s_app.sound)) hal_audio_idle();
}

static void route_softkey(int col) {
    app_sound(SND_BLIP);
    switch (s_app.screen) {
        case SCREEN_TIMER:    screen_timer_softkey(col);    break;
        case SCREEN_SETTINGS: screen_settings_softkey(col); break;
        case SCREEN_NEARBY:   screen_nearby_softkey(col);   break;
    }
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

    /* beacon rx -> neighbor table (Plan C makes tx/rx real; sim fakes rx) */
    uint8_t wire[BEACON_WIRE_LEN];
    if (hal_beacon_rx(wire)) { beacon_msg_t m; if (beacon_unpack(wire, &m)) neighbor_upsert(&s_app.neighbors, &m, now); }
    neighbor_expire(&s_app.neighbors, now);

    /* auto-dim: poll lux at ~2 Hz through the core dimming curve */
    if (now >= s_next_lux) {
        float lux;
        if (hal_lux(&lux)) {
            uint8_t pct = dim_apply(&s_dim, lux);
            hal_backlight(pct);
            hal_led_brightness((uint8_t)((uint32_t)dim_led_brightness(pct) * LED_BRIGHT_MAX / 255));
        }
        s_next_lux = now + 500;
    }
    /* per-theme LED pattern */
    timer_view_t lv = timer_view_make(&s_app.pomo, s_app.alarm_active, now);
    led_rgb_t leds[LED_COUNT];
    led_pattern_render(s_app.settings.theme, &lv, leds);
    for (int i = 0; i < LED_COUNT; i++) hal_led_set(i, leds[i].r, leds[i].g, leds[i].b);
    hal_led_show();

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
    s_next_lux = 0;
    s_app.screen = SCREEN_TIMER; s_app.alarm_active = false;
    sound_reset(&s_app.sound);
    s_next_tick_ms = 0; s_alarm_next_ms = 0;

    s_scr[SCREEN_TIMER]    = screen_timer_create();
    s_scr[SCREEN_SETTINGS] = screen_settings_create();
    s_scr[SCREEN_NEARBY]   = screen_nearby_create();
    lv_screen_load(s_scr[SCREEN_TIMER]);

    lv_timer_create(tick_cb, 200, NULL);
    lv_timer_create(sound_cb, 20, NULL);
}
