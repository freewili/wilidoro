#include "app.h"
#include "hal.h"
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

static lv_obj_t *s_scr[3];
static dim_state_t s_dim;
static uint32_t s_next_lux;

static void route_softkey(int col) {
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
    if (ev == PM_EV_FOCUS_ENDED) { s_app.alarm_active = true; hal_tone(880, 200, 200); }
    if (ev == PM_EV_BREAK_ENDED) { hal_tone(660, 120, 160); }

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
            hal_led_brightness(dim_led_brightness(pct));
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

    s_scr[SCREEN_TIMER]    = screen_timer_create();
    s_scr[SCREEN_SETTINGS] = screen_settings_create();
    s_scr[SCREEN_NEARBY]   = screen_nearby_create();
    lv_screen_load(s_scr[SCREEN_TIMER]);

    lv_timer_create(tick_cb, 200, NULL);
}
