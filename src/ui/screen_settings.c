#include "screen_settings.h"
#include "ui.h"
#include "app.h"
#include "app_model.h"
#include "screen_timer.h"
#include "theme.h"
#include "hal.h"
#include <stdio.h>

static lv_obj_t *s_scr, *s_list, *s_bar, *s_title;
static lv_obj_t *s_val_focus, *s_val_short, *s_val_long, *s_val_vol, *s_val_beacon, *s_val_theme, *s_val_dvi, *s_val_tilt;

static void refresh_values(void) {
    app_settings_t *s = &app()->settings; char b[16];
    snprintf(b,sizeof b,"%u min",s->focus_min); lv_label_set_text(s_val_focus,b);
    snprintf(b,sizeof b,"%u min",s->short_min); lv_label_set_text(s_val_short,b);
    snprintf(b,sizeof b,"%u min",s->long_min);  lv_label_set_text(s_val_long,b);
    snprintf(b,sizeof b,"%u%%",s->volume);      lv_label_set_text(s_val_vol,b);
    lv_label_set_text(s_val_beacon, s->beacon_on?"on":"off");
    lv_label_set_text(s_val_dvi, s->dvi_on?"on":"off");
    /* "no imu" rather than "off" when the BMI323 never came up, so a dead
       sensor is visible instead of a toggle that silently does nothing. */
    lv_label_set_text(s_val_tilt, hal_caps().imu ? (s->tilt_pause?"on":"off") : "no imu");
    static const char *tn[3]={"Neon Arc","Arcade","Flip Clock"};
    lv_label_set_text(s_val_theme, tn[s->theme%3]);
}

/* Each row: [label] [-] [value] [+]. user_data on +/- encodes which setting & sign. */
enum { SET_FOCUS=1, SET_SHORT, SET_LONG, SET_VOL, SET_BEACON, SET_THEME, SET_DVI, SET_TILT };
static void adj_event(lv_event_t *e) {
    intptr_t code = (intptr_t)lv_event_get_user_data(e);
    int which = (int)(code >> 1); int sign = (code & 1) ? +1 : -1;
    app_settings_t *s = &app()->settings;
    switch (which) {
        case SET_FOCUS: app_settings_adjust_focus(s, sign); break;
        case SET_SHORT: app_settings_adjust_short(s, sign); break;
        case SET_LONG:  app_settings_adjust_long(s, sign);  break;
        case SET_VOL:   app_settings_adjust_volume(s, sign);break;
        case SET_BEACON:s->beacon_on = !s->beacon_on; break;
        case SET_THEME: app_settings_cycle_theme(s); screen_timer_apply_theme(); break;
        case SET_DVI:   s->dvi_on = !s->dvi_on; app_dvi_apply(); break;
        case SET_TILT:  s->tilt_pause = !s->tilt_pause; app_tilt_apply(); break;
    }
    refresh_values();
}

static lv_obj_t *add_row(const char *name, int which) {
    lv_obj_t *row = lv_obj_create(s_list);
    lv_obj_set_size(row, lv_pct(100), UI_ROW_H);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);   /* let the LIST scroll, not the row */
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);    /* flat: no box around each setting */
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_border_width(row, 1, 0);          /* thin bottom divider instead of a box */
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(UI_PANEL), 0);
    lv_obj_set_style_pad_top(row, 4, 0);
    lv_obj_set_style_pad_bottom(row, 4, 0);
    lv_obj_set_style_pad_left(row, 8, 0);
    lv_obj_set_style_pad_right(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *l = lv_label_create(row); lv_label_set_text(l,name);
    lv_obj_set_style_text_color(l, lv_color_hex(UI_TEXT), 0); lv_obj_set_width(l, UI_SET_LABEL_W);
    lv_obj_t *minus = lv_button_create(row); lv_obj_set_size(minus,UI_STEP_BTN_W,UI_STEP_BTN_H);
    lv_obj_add_event_cb(minus, adj_event, LV_EVENT_CLICKED, (void*)(intptr_t)((which<<1)|0));
    lv_obj_t *ml=lv_label_create(minus); lv_label_set_text(ml,"-"); lv_obj_center(ml);
    lv_obj_t *val = lv_label_create(row); lv_obj_set_width(val, UI_SET_VAL_W);
    /* Plain white, not UI_ACCENT: the saturated coral was picked as an accent for
       a neutral mock and is hard to read as body text on the dark list once the
       auto-dim backlight drops. These values are the thing you actually read. */
    lv_obj_set_style_text_color(val, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *plus = lv_button_create(row); lv_obj_set_size(plus,UI_STEP_BTN_W,UI_STEP_BTN_H);
    lv_obj_add_event_cb(plus, adj_event, LV_EVENT_CLICKED, (void*)(intptr_t)((which<<1)|1));
    lv_obj_t *pl=lv_label_create(plus); lv_label_set_text(pl,"+"); lv_obj_center(pl);
    return val;
}

lv_obj_t *screen_settings_create(void) {
    s_scr = ui_screen();
    s_title = lv_label_create(s_scr); lv_label_set_text(s_title,"SETTINGS");
    lv_obj_set_style_text_color(s_title, lv_color_hex(UI_MUTED),0);
    lv_obj_align(s_title, LV_ALIGN_TOP_LEFT, 12, 8);

    s_list = lv_obj_create(s_scr);
    lv_obj_set_size(s_list, UI_LIST_W, UI_LIST_H);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 2, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);   /* visible scroll cue */

    s_val_focus  = add_row("Focus length", SET_FOCUS);
    s_val_short  = add_row("Short break",  SET_SHORT);
    s_val_long   = add_row("Long break",   SET_LONG);
    s_val_vol    = add_row("Volume",       SET_VOL);
    s_val_beacon = add_row("Beacon",       SET_BEACON);
    s_val_theme  = add_row("Theme",        SET_THEME);
    s_val_dvi    = add_row("DVI output",   SET_DVI);
    s_val_tilt   = add_row("Tilt to pause", SET_TILT);

    s_bar = ui_softkey_bar(s_scr, screen_settings_softkey);
    const char *lbl[5] = {"Back", 0, "Default", 0, "Save"};
    ui_softkey_set_labels(s_bar, lbl);
    refresh_values();
    return s_scr;
}

void screen_settings_update(void) {
    lv_obj_set_style_text_color(s_title, lv_color_hex(theme_get(app()->settings.theme)->accent), 0);
}

void screen_settings_softkey(int col) {
    app_t *a = app();
    if (col==0) { app_goto(SCREEN_TIMER); }
    else if (col==2) { app_settings_defaults(&a->settings); refresh_values(); screen_timer_apply_theme(); app_dvi_apply(); app_tilt_apply(); }
    else if (col==4) {
        pm_config_t c = { a->settings.focus_min, a->settings.short_min, a->settings.long_min, a->settings.long_every };
        if (a->pomo.state == PM_IDLE) pomodoro_init(&a->pomo, c); /* apply only when idle to avoid mid-session surprise */
        app_goto(SCREEN_TIMER);
    }
}
