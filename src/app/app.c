#include "app.h"
#include "hal.h"
#include "ui.h"
#include "screen_timer.h"
#include "screen_settings.h"
#include "screen_nearby.h"
#include "lvgl.h"

static app_t s_app;
app_t *app(void) { return &s_app; }

static lv_obj_t *s_scr[3];

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

    /* simple phase LED color (Plan C replaces with theme patterns) */
    uint8_t r=0,g=0,bl=0;
    if (s_app.pomo.state == PM_FOCUS)      { r=255; g=90; bl=40; }
    else if (s_app.pomo.state==PM_BREAK_SHORT||s_app.pomo.state==PM_BREAK_LONG){ r=40; g=200; bl=180; }
    for (int i=0;i<16;i++) hal_led_set(i, r, g, bl);
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
    s_app.screen = SCREEN_TIMER; s_app.alarm_active = false;

    s_scr[SCREEN_TIMER]    = screen_timer_create();
    s_scr[SCREEN_SETTINGS] = screen_settings_create();
    s_scr[SCREEN_NEARBY]   = screen_nearby_create();
    lv_screen_load(s_scr[SCREEN_TIMER]);

    lv_timer_create(tick_cb, 200, NULL);
}
