#include "screen_timer.h"
#include "ui.h"
#include "app.h"
#include "hal.h"
#include "theme.h"
#include "timer_view.h"
#include "pomodoro.h"

static lv_obj_t *s_scr, *s_face, *s_bar;
static const theme_t *s_theme;

static void on_softkey(int col) { screen_timer_softkey(col); }

void screen_timer_apply_theme(void) {
    s_theme = theme_get(app()->settings.theme);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(s_theme->bg), 0);
    lv_obj_clean(s_face);
    s_theme->build(s_face);
}

lv_obj_t *screen_timer_create(void) {
    s_scr = ui_screen();
    s_face = lv_obj_create(s_scr);
    lv_obj_set_size(s_face, 480, 286);
    lv_obj_align(s_face, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_face, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_face, 0, 0);
    lv_obj_set_style_pad_all(s_face, 0, 0);
    lv_obj_remove_flag(s_face, LV_OBJ_FLAG_SCROLLABLE);

    s_bar = ui_softkey_bar(s_scr, on_softkey);
    screen_timer_apply_theme();     /* builds the initial face + bg */
    screen_timer_update();
    return s_scr;
}

static void labels_for_state(const timer_view_t *v, const char *out[5]) {
    static const char *idle[5]  = {"Start", 0, 0, "Nearby", "Menu"};
    static const char *run[5]   = {"Pause", "Skip", "+5", "Nearby", "Menu"};
    static const char *paused[5]= {"Resume","Skip", "+5", "Nearby", "Menu"};
    static const char *alarm_l[5]={"Dismiss",0,0,0,"Menu"};
    const char **src = idle;
    if (v->alarm) src = alarm_l;
    else if (v->paused) src = paused;
    else if (!v->idle) src = run;
    for (int i=0;i<5;i++) out[i]=src[i];
}

void screen_timer_update(void) {
    timer_view_t v = timer_view_make(&app()->pomo, app()->alarm_active, hal_now_ms());
    s_theme->update(&v);
    const char *lbl[5]; labels_for_state(&v, lbl);
    ui_softkey_set_labels(s_bar, lbl);
}

void screen_timer_softkey(int col) {
    app_t *a = app();
    uint32_t now = hal_now_ms();
    if (a->alarm_active) {
        if (col==0) { pomodoro_acknowledge(&a->pomo, now); a->alarm_active=false; app_sound_stop(); }
        else if (col==4) app_goto(SCREEN_SETTINGS);
        return;
    }
    switch (a->pomo.state) {
        case PM_IDLE:
            if (col==0) { pomodoro_start_focus(&a->pomo, now); app_sound(SND_START); }
            else if (col==3) app_goto(SCREEN_NEARBY);
            else if (col==4) app_goto(SCREEN_SETTINGS);
            break;
        case PM_PAUSED:
            if (col==0) pomodoro_resume(&a->pomo, now);
            else if (col==1) pomodoro_skip(&a->pomo, now);
            else if (col==2) pomodoro_add5(&a->pomo, now);
            else if (col==3) app_goto(SCREEN_NEARBY);
            else if (col==4) app_goto(SCREEN_SETTINGS);
            break;
        default:
            if (col==0) pomodoro_pause(&a->pomo, now);
            else if (col==1) pomodoro_skip(&a->pomo, now);
            else if (col==2) pomodoro_add5(&a->pomo, now);
            else if (col==3) app_goto(SCREEN_NEARBY);
            else if (col==4) app_goto(SCREEN_SETTINGS);
            break;
    }
}
