#include "screen_timer.h"
#include "ui.h"
#include "app.h"
#include "hal.h"
#include "pomodoro.h"
#include <stdio.h>

static lv_obj_t *s_scr, *s_arc, *s_time, *s_state, *s_bar;

static void on_softkey(int col) { screen_timer_softkey(col); }

lv_obj_t *screen_timer_create(void) {
    s_scr = ui_screen();

    s_arc = lv_arc_create(s_scr);
    lv_obj_set_size(s_arc, 220, 220);
    lv_obj_align(s_arc, LV_ALIGN_CENTER, 0, -8);
    lv_arc_set_rotation(s_arc, 270);
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_arc_set_range(s_arc, 0, 1000);
    lv_arc_set_value(s_arc, 1000);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_arc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(UI_PANEL), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(UI_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_INDICATOR);

    s_time = lv_label_create(s_scr);
    lv_obj_set_style_text_color(s_time, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_text_font(s_time, &lv_font_montserrat_48, 0);
    lv_obj_align(s_time, LV_ALIGN_CENTER, 0, -18);
    lv_label_set_text(s_time, "25:00");

    s_state = lv_label_create(s_scr);
    lv_obj_set_style_text_color(s_state, lv_color_hex(UI_MUTED), 0);
    lv_obj_set_style_text_font(s_state, &lv_font_montserrat_16, 0);
    lv_obj_align(s_state, LV_ALIGN_CENTER, 0, 24);
    lv_label_set_text(s_state, "READY");

    s_bar = ui_softkey_bar(s_scr, on_softkey);
    screen_timer_update();
    return s_scr;
}

static void labels_for_state(pm_state_t st, bool alarm, const char *out[5]) {
    static const char *idle[5]  = {"Start", 0, 0, "Nearby", "Menu"};
    static const char *run[5]   = {"Pause", "Skip", "+5", "Nearby", "Menu"};
    static const char *paused[5]= {"Resume","Skip", "+5", "Nearby", "Menu"};
    static const char *alarm_l[5]={"Dismiss",0,0,0,"Menu"};
    const char **src = idle;
    if (alarm) src = alarm_l;
    else if (st == PM_FOCUS || st == PM_BREAK_SHORT || st == PM_BREAK_LONG) src = run;
    else if (st == PM_PAUSED) src = paused;
    for (int i=0;i<5;i++) out[i]=src[i];
}

void screen_timer_update(void) {
    app_t *a = app();
    uint32_t now = hal_now_ms();
    uint32_t rem = pomodoro_remaining_ms(&a->pomo, now);
    uint32_t total_ms = (uint32_t)a->pomo.cfg.focus_min * 60000u;
    if (a->pomo.state==PM_BREAK_SHORT) total_ms=(uint32_t)a->pomo.cfg.short_min*60000u;
    else if (a->pomo.state==PM_BREAK_LONG) total_ms=(uint32_t)a->pomo.cfg.long_min*60000u;

    char buf[8]; unsigned s = rem/1000; snprintf(buf, sizeof buf, "%02u:%02u", s/60, s%60);
    lv_label_set_text(s_time, buf);

    int32_t val = total_ms ? (int32_t)((uint64_t)rem*1000/total_ms) : 0;
    lv_arc_set_value(s_arc, val);
    bool break_phase = (a->pomo.state==PM_BREAK_SHORT||a->pomo.state==PM_BREAK_LONG);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(break_phase?UI_COOL:UI_ACCENT), LV_PART_INDICATOR);

    const char *name = a->alarm_active ? "TIME'S UP" :
        a->pomo.state==PM_FOCUS?"FOCUS":
        (break_phase?"BREAK":
        (a->pomo.state==PM_PAUSED?"PAUSED":"READY"));
    char st[24]; snprintf(st, sizeof st, "%s  %u/%u", name,
        (unsigned)(a->pomo.stats.completed % a->pomo.cfg.long_every),
        (unsigned)a->pomo.cfg.long_every);
    lv_label_set_text(s_state, st);

    const char *lbl[5]; labels_for_state(a->pomo.state, a->alarm_active, lbl);
    ui_softkey_set_labels(s_bar, lbl);
}

void screen_timer_softkey(int col) {
    app_t *a = app();
    uint32_t now = hal_now_ms();
    if (a->alarm_active) {
        if (col==0) { pomodoro_acknowledge(&a->pomo, now); a->alarm_active=false; }
        else if (col==4) app_goto(SCREEN_SETTINGS);
        return;
    }
    switch (a->pomo.state) {
        case PM_IDLE:
            if (col==0) pomodoro_start_focus(&a->pomo, now);
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
        default: /* FOCUS/BREAK */
            if (col==0) pomodoro_pause(&a->pomo, now);
            else if (col==1) pomodoro_skip(&a->pomo, now);
            else if (col==2) pomodoro_add5(&a->pomo, now);
            else if (col==3) app_goto(SCREEN_NEARBY);
            else if (col==4) app_goto(SCREEN_SETTINGS);
            break;
    }
}
