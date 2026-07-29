#include "theme.h"
#include "ui.h"
#include <stdio.h>

#define FL_BG1   0x221f1d
#define FL_BG2   0x141210
#define FL_CARD  0x141210
#define FL_SEAM  0x2e2a26
#define FL_TEXT  0xf0e6d6
#define FL_MUTED 0x8a7d6b
#define FL_ACCENT 0xC8503C

static lv_obj_t *s_mm, *s_ss, *s_status, *s_bar, *s_title;

static lv_obj_t *card(lv_obj_t *par, int x) {
    lv_obj_t *c = lv_obj_create(par);
    lv_obj_set_size(c, UI_FLIP_CARD, UI_FLIP_CARD);
    lv_obj_align(c, LV_ALIGN_CENTER, x, -14);
    lv_obj_set_style_bg_color(c, lv_color_hex(FL_CARD), 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    /* mid seam */
    lv_obj_t *seam = lv_obj_create(c);
    lv_obj_set_size(seam, UI_FLIP_CARD, 3);
    lv_obj_align(seam, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(seam, lv_color_hex(FL_SEAM), 0);
    lv_obj_set_style_border_width(seam, 0, 0);
    return c;
}

static void fl_build(lv_obj_t *face) {
    s_title = lv_label_create(face);
    lv_obj_set_style_text_color(s_title, lv_color_hex(FL_MUTED), 0);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_16, 0);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 12);
    lv_label_set_text(s_title, "focus");

    lv_obj_t *cm = card(face, -UI_FLIP_CARD_X);
    s_mm = lv_label_create(cm);
    lv_obj_set_style_text_color(s_mm, lv_color_hex(FL_TEXT), 0);
    lv_obj_set_style_text_font(s_mm, UI_FONT_BIG, 0);
    lv_obj_center(s_mm); lv_label_set_text(s_mm, "25");

    lv_obj_t *cs = card(face, UI_FLIP_CARD_X);
    s_ss = lv_label_create(cs);
    lv_obj_set_style_text_color(s_ss, lv_color_hex(FL_TEXT), 0);
    lv_obj_set_style_text_font(s_ss, UI_FONT_BIG, 0);
    lv_obj_center(s_ss); lv_label_set_text(s_ss, "00");

    /* colon */
    lv_obj_t *colon = lv_label_create(face);
    lv_obj_set_style_text_color(colon, lv_color_hex(FL_TEXT), 0);
    lv_obj_set_style_text_font(colon, UI_FONT_BIG, 0);
    lv_obj_align(colon, LV_ALIGN_CENTER, 0, -20);
    lv_label_set_text(colon, ":");

    s_bar = lv_bar_create(face);
    lv_obj_set_size(s_bar, UI_FLIP_BAR_W, 8);
    lv_obj_align(s_bar, LV_ALIGN_CENTER, 0, UI_FLIP_BAR_Y);
    lv_bar_set_range(s_bar, 0, 1000);
    lv_bar_set_value(s_bar, 1000, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(FL_CARD), LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(FL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, 4, LV_PART_INDICATOR);

    s_status = lv_label_create(face);
    lv_obj_set_style_text_color(s_status, lv_color_hex(FL_MUTED), 0);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_16, 0);
    lv_obj_align(s_status, LV_ALIGN_CENTER, 0, UI_FLIP_STATUS_Y);
    lv_label_set_text(s_status, "ready when you are");
}

static void fl_update(const timer_view_t *v) {
    unsigned s = v->rem_ms/1000;
    char mm[4], ss[4]; snprintf(mm, sizeof mm, "%02u", s/60); snprintf(ss, sizeof ss, "%02u", s%60);
    lv_label_set_text(s_mm, mm);
    lv_label_set_text(s_ss, ss);

    int32_t val = v->total_ms ? (int32_t)((uint64_t)v->rem_ms*1000/v->total_ms) : 0;
    lv_bar_set_value(s_bar, val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(v->break_phase?FL_MUTED:FL_ACCENT), LV_PART_INDICATOR);

    lv_label_set_text(s_title, v->break_phase ? "break" : (v->idle||v->alarm ? "pomodoro" : "focus"));
    const char *st = v->alarm ? "time's up — take a breath" :
        v->paused ? "paused" :
        v->idle ? "ready when you are" :
        (v->break_phase ? "rest your eyes" : "stay with it");
    lv_label_set_text(s_status, st);
}

const theme_t THEME_FLIP = {
    .name="Flip Clock", .bg=FL_BG1, .accent=FL_ACCENT, .cool=FL_MUTED,
    .build=fl_build, .update=fl_update,
};
