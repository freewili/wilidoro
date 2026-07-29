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

#if defined(WILIDORO_BOARD_OG)
/* OG only: no touchscreen, so the per-row +/- buttons never receive
   LV_EVENT_CLICKED. Settings is instead driven from the five-button softkey
   bar (see screen_settings_softkey below), which needs the row objects and
   each row's `which` enum to move a selection and adjust it. */
/* No DVI row on this board (hal_caps().dvi is always false, hal_dvi_surface()
   always returns false -- see hal_og.c) -- add_row("DVI output", ...) below is
   compiled out on the OG, so this is 7 rows here, not 8. */
enum { N_SET_ROWS = 7 };
static lv_obj_t *s_row_obj[N_SET_ROWS];
static int       s_row_which[N_SET_ROWS];
static int       s_row_count = 0;
static int       s_sel = 0;
#endif

static void refresh_values(void) {
    app_settings_t *s = &app()->settings; char b[16];
    snprintf(b,sizeof b,"%u min",s->focus_min); lv_label_set_text(s_val_focus,b);
    snprintf(b,sizeof b,"%u min",s->short_min); lv_label_set_text(s_val_short,b);
    snprintf(b,sizeof b,"%u min",s->long_min);  lv_label_set_text(s_val_long,b);
    snprintf(b,sizeof b,"%u%%",s->volume);      lv_label_set_text(s_val_vol,b);
    lv_label_set_text(s_val_beacon, s->beacon_on?"on":"off");
#if !defined(WILIDORO_BOARD_OG)
    /* No DVI row -> no s_val_dvi object on the OG; see add_row() below. */
    lv_label_set_text(s_val_dvi, s->dvi_on?"on":"off");
#endif
    /* "no imu" rather than "off" when the BMI323 never came up, so a dead
       sensor is visible instead of a toggle that silently does nothing. */
    lv_label_set_text(s_val_tilt, hal_caps().imu ? (s->tilt_pause?"on":"off") : "no imu");
    static const char *tn[3]={"Neon Arc","Arcade","Flip Clock"};
    lv_label_set_text(s_val_theme, tn[s->theme%3]);
}

/* Each row: [label] [-] [value] [+]. user_data on +/- encodes which setting & sign. */
enum { SET_FOCUS=1, SET_SHORT, SET_LONG, SET_VOL, SET_BEACON, SET_THEME, SET_DVI, SET_TILT };

/* The single definition of what adjusting a setting means. Shared by the FW2
   touch path (adj_event, below) and the OG's Left/Right softkey columns, so
   there is exactly one switch table to keep in sync -- not two that can
   drift. SET_THEME ignores `sign` (app_settings_cycle_theme() only cycles
   forward), so on the OG both Left and Right advance the theme; that is a
   known, accepted asymmetry -- see screen_settings_softkey(). */
static void apply_adjust(int which, int sign) {
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
static void adj_event(lv_event_t *e) {
    intptr_t code = (intptr_t)lv_event_get_user_data(e);
    int which = (int)(code >> 1); int sign = (code & 1) ? +1 : -1;
    apply_adjust(which, sign);
}

static lv_obj_t *add_row(const char *name, int which) {
    lv_obj_t *row = lv_obj_create(s_list);
#if defined(WILIDORO_BOARD_OG)
    /* N_SET_ROWS sizes s_row_obj/s_row_which; a 9th add_row() call would
       overrun them and silently corrupt whichever statics follow. Refuse the
       write instead -- the row still renders (added to s_list above), it
       just will not be reachable from the arrow-pad's Up/Down nav. */
    if (s_row_count < N_SET_ROWS) {
        s_row_obj[s_row_count] = row;
        s_row_which[s_row_count] = which;
        s_row_count++;
    }
#endif
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

#if defined(WILIDORO_BOARD_OG)
/* Move the selection to row `idx` (wrapping both directions), give it an
   unmistakable highlight, and scroll it into view so rows 5-8, which sit
   below the fold on a 320x240 panel, become reachable.
   UI_PANEL-on-UI_BG (the old background-only highlight) differs by only
   ~1-3 LSBs after RGB565 quantisation on the physical panel -- confirmed
   "really hard to see" on hardware. The highlight is now a filled
   background plus a left accent bar, in the current theme's accent colour
   (theme_get()->accent, the same source screen_settings_update() uses for
   the title) so it tracks theme changes. The row's border_side gains
   LV_BORDER_SIDE_LEFT alongside the existing LV_BORDER_SIDE_BOTTOM divider
   add_row() set up -- overwriting border_side with LEFT alone would have
   silently dropped that divider, so BOTTOM stays OR'd in throughout. */
static void select_row(int idx) {
    /* Defensive only: every board today calls add_row() unconditionally 7
       times, so s_row_count is never 0 by the time this runs. But nothing
       stops a future conditionally-compiled row (e.g. Beacon, gated on
       hal_caps().radio) from being the LAST row left standing on some
       future board variant with none at all -- without this, idx wrapping
       to s_row_count-1 below would be -1, indexing s_row_obj[-1]. */
    if (s_row_count <= 0) return;
    /* Wrap on s_row_count, the number of rows actually filled in, NOT
       N_SET_ROWS -- add_row() (above) deliberately tolerates under-filling
       (`if (s_row_count < N_SET_ROWS)`), which makes N_SET_ROWS a capacity,
       not a count. The two are equal today (7 rows, 7 slots), but the next
       conditionally-compiled row (e.g. a future Beacon row gated on
       hal_caps().radio, which is false on the OG) would leave
       s_row_obj[N_SET_ROWS-1] == NULL, and wrapping on N_SET_ROWS would
       dereference it below -- hard fault. */
    if (idx < 0) idx = s_row_count - 1;
    else if (idx >= s_row_count) idx = 0;

    /* Clear the old row's selected treatment completely before reassigning
       s_sel, so nothing accumulates as the selection moves down the list. */
    lv_obj_t *old = s_row_obj[s_sel];
    lv_obj_set_style_bg_opa(old, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_side(old, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(old, 1, 0);
    lv_obj_set_style_border_color(old, lv_color_hex(UI_PANEL), 0);

    s_sel = idx;
    lv_obj_t *row = s_row_obj[s_sel];
    uint32_t accent = theme_get(app()->settings.theme)->accent;
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_PANEL), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(row, 3, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(accent), 0);
    lv_obj_scroll_to_view(row, LV_ANIM_OFF);
}
#endif

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
#if !defined(WILIDORO_BOARD_OG)
    /* The OG has no DVI hardware (hal_caps().dvi is false, hal_dvi_surface()
       always returns false), so this row would read "DVI output: on" and let
       the user "adjust" something with no effect. FW2 keeps it exactly as
       before. Do not remove SET_DVI from the enum or app_dvi_apply() below --
       the FW2 still uses both. */
    s_val_dvi    = add_row("DVI output",   SET_DVI);
#endif
    s_val_tilt   = add_row("Tilt to pause", SET_TILT);

    s_bar = ui_softkey_bar(s_scr, screen_settings_softkey);
#if defined(WILIDORO_BOARD_OG)
    /* Arrow-pad layout per wiliOGbsp/AGENTS.md: grey/red move the selection,
       yellow/blue adjust it, green applies and returns. */
    const char *lbl[5] = {"Up", "-", "OK", "+", "Down"};
#else
    const char *lbl[5] = {"Back", 0, "Default", 0, "Save"};
#endif
    ui_softkey_set_labels(s_bar, lbl);
    refresh_values();
#if defined(WILIDORO_BOARD_OG)
    /* The screen is built once in app_init() and reused, so this only runs
       on the very first visit; the selection persists across later visits
       by design (nothing re-selects row 0 on entry). */
    select_row(0);
#endif
    return s_scr;
}

void screen_settings_update(void) {
    uint32_t accent = theme_get(app()->settings.theme)->accent;
    lv_obj_set_style_text_color(s_title, lv_color_hex(accent), 0);
#if defined(WILIDORO_BOARD_OG)
    /* Keep the selected row's accent bar in sync if the theme changes while
       that row (e.g. "Theme" itself) is selected -- select_row() only
       stamps the accent at selection time, and this runs every frame while
       Settings is on screen, same as the title above it. select_row() now
       always wraps within s_row_count, so s_row_obj[s_sel] should never be
       NULL, but this runs unconditionally every frame regardless of how
       s_sel got set, so guard it defensively rather than trust that. */
    if (s_row_obj[s_sel]) lv_obj_set_style_border_color(s_row_obj[s_sel], lv_color_hex(accent), 0);
#endif
}

void screen_settings_softkey(int col) {
#if defined(WILIDORO_BOARD_OG)
    /* Arrow pad per wiliOGbsp/AGENTS.md: softkey columns 0..4 are the
       physical GRAY,YELLOW,GREEN,BLUE,RED buttons, left to right (see
       hal_og.c). Red's short press lands here as Down; its ~6s hold is the
       BSP power-off gesture handled entirely in hal_pump(), untouched by
       this switch. There is no slot for "Default" or "Back" on five
       buttons -- OK covers leaving, and Settings mutates app()->settings
       live so there was never anything for Back to discard. */
    app_t *a = app();
    switch (col) {
        case 0: select_row(s_sel - 1); break;                  /* grey: Up */
        case 1: apply_adjust(s_row_which[s_sel], -1); break;   /* yellow: Left */
        case 2: {                                               /* green: OK */
            if (a->pomo.state == PM_IDLE) app_apply_settings_to_pomo(); /* apply only when idle to avoid mid-session surprise */
            app_goto(SCREEN_TIMER);
            break;
        }
        case 3: apply_adjust(s_row_which[s_sel], +1); break;   /* blue: Right */
        case 4: select_row(s_sel + 1); break;                  /* red: Down */
    }
#else
    app_t *a = app();
    if (col==0) { app_goto(SCREEN_TIMER); }
    else if (col==2) { app_settings_defaults(&a->settings); refresh_values(); screen_timer_apply_theme(); app_dvi_apply(); app_tilt_apply(); }
    else if (col==4) {
        if (a->pomo.state == PM_IDLE) app_apply_settings_to_pomo(); /* apply only when idle to avoid mid-session surprise */
        app_goto(SCREEN_TIMER);
    }
#endif
}
