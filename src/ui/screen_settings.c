#include "screen_settings.h"
#include "ui.h"
#include "app.h"
#include "app_model.h"
#include <stdio.h>

static lv_obj_t *s_scr, *s_list, *s_bar;
static lv_obj_t *s_val_focus, *s_val_short, *s_val_long, *s_val_vol, *s_val_beacon, *s_val_theme;

static void refresh_values(void) {
    app_settings_t *s = &app()->settings; char b[16];
    snprintf(b,sizeof b,"%u min",s->focus_min); lv_label_set_text(s_val_focus,b);
    snprintf(b,sizeof b,"%u min",s->short_min); lv_label_set_text(s_val_short,b);
    snprintf(b,sizeof b,"%u min",s->long_min);  lv_label_set_text(s_val_long,b);
    snprintf(b,sizeof b,"%u%%",s->volume);      lv_label_set_text(s_val_vol,b);
    lv_label_set_text(s_val_beacon, s->beacon_on?"on":"off");
    static const char *tn[3]={"Neon Arc","Arcade","Flip Clock"};
    lv_label_set_text(s_val_theme, tn[s->theme%3]);
}

/* Each row: [label] [-] [value] [+]. user_data on +/- encodes which setting & sign. */
enum { SET_FOCUS=1, SET_SHORT, SET_LONG, SET_VOL, SET_BEACON, SET_THEME };
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
        case SET_THEME: app_settings_cycle_theme(s); break;
    }
    refresh_values();
}

static lv_obj_t *add_row(const char *name, int which) {
    lv_obj_t *row = lv_obj_create(s_list);
    lv_obj_set_size(row, 460, 40);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_PANEL), 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *l = lv_label_create(row); lv_label_set_text(l,name);
    lv_obj_set_style_text_color(l, lv_color_hex(UI_TEXT), 0); lv_obj_set_width(l, 230);
    lv_obj_t *minus = lv_button_create(row); lv_obj_set_size(minus,34,30);
    lv_obj_add_event_cb(minus, adj_event, LV_EVENT_CLICKED, (void*)(intptr_t)((which<<1)|0));
    lv_obj_t *ml=lv_label_create(minus); lv_label_set_text(ml,"-"); lv_obj_center(ml);
    lv_obj_t *val = lv_label_create(row); lv_obj_set_width(val, 90);
    lv_obj_set_style_text_color(val, lv_color_hex(UI_ACCENT), 0);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *plus = lv_button_create(row); lv_obj_set_size(plus,34,30);
    lv_obj_add_event_cb(plus, adj_event, LV_EVENT_CLICKED, (void*)(intptr_t)((which<<1)|1));
    lv_obj_t *pl=lv_label_create(plus); lv_label_set_text(pl,"+"); lv_obj_center(pl);
    return val;
}

lv_obj_t *screen_settings_create(void) {
    s_scr = ui_screen();
    lv_obj_t *title = lv_label_create(s_scr); lv_label_set_text(title,"SETTINGS");
    lv_obj_set_style_text_color(title, lv_color_hex(UI_MUTED),0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 8);

    s_list = lv_obj_create(s_scr);
    lv_obj_set_size(s_list, 476, 236);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(UI_BG), 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);

    s_val_focus  = add_row("Focus length", SET_FOCUS);
    s_val_short  = add_row("Short break",  SET_SHORT);
    s_val_long   = add_row("Long break",   SET_LONG);
    s_val_vol    = add_row("Volume",       SET_VOL);
    s_val_beacon = add_row("Beacon",       SET_BEACON);
    s_val_theme  = add_row("Theme",        SET_THEME);

    s_bar = ui_softkey_bar(s_scr, screen_settings_softkey);
    const char *lbl[5] = {"Back", 0, "Default", 0, "Save"};
    ui_softkey_set_labels(s_bar, lbl);
    refresh_values();
    return s_scr;
}

void screen_settings_update(void) { /* values refresh on edit; nothing periodic */ }

void screen_settings_softkey(int col) {
    app_t *a = app();
    if (col==0) { app_goto(SCREEN_TIMER); }
    else if (col==2) { app_settings_defaults(&a->settings); refresh_values(); }
    else if (col==4) {
        pm_config_t c = { a->settings.focus_min, a->settings.short_min, a->settings.long_min, a->settings.long_every };
        if (a->pomo.state == PM_IDLE) pomodoro_init(&a->pomo, c); /* apply only when idle to avoid mid-session surprise */
        app_goto(SCREEN_TIMER);
    }
}
