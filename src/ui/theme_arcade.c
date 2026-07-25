#include "theme.h"
#include <stdio.h>

#define AR_BG     0x2d1b4e
#define AR_PANEL  0x1a0f33
#define AR_RED    0xff4757
#define AR_GREEN  0x2ed573
#define AR_YELLOW 0xffe066
#define AR_PINK   0xff6b81

static lv_obj_t *s_time, *s_status, *s_bar, *s_score, *s_body;

static lv_obj_t *rect(lv_obj_t *par, int w,int h,int x,int y,uint32_t col,int radius){
    lv_obj_t *o = lv_obj_create(par);
    lv_obj_set_size(o,w,h); lv_obj_align(o,LV_ALIGN_TOP_LEFT,x,y);
    lv_obj_set_style_bg_color(o,lv_color_hex(col),0);
    lv_obj_set_style_border_width(o,0,0);
    lv_obj_set_style_radius(o,radius,0);
    lv_obj_set_style_pad_all(o,0,0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void ar_build(lv_obj_t *face) {
    /* mascot: body + stem + eyes, top-left area */
    s_body = rect(face, 96, 80, 40, 60, AR_RED, 26);
    rect(face, 12, 20, 62, 46, AR_GREEN, 3);
    rect(face, 12, 26, 82, 40, AR_GREEN, 3);
    rect(face, 12, 20, 102, 46, AR_GREEN, 3);
    rect(face, 12, 12, 66, 92, AR_BG, 3);   /* left eye */
    rect(face, 12, 12, 100, 92, AR_BG, 3);  /* right eye */

    /* big time, right of mascot */
    s_time = lv_label_create(face);
    lv_obj_set_style_text_color(s_time, lv_color_hex(AR_YELLOW), 0);
    lv_obj_set_style_text_font(s_time, &lv_font_montserrat_48, 0);
    lv_obj_align(s_time, LV_ALIGN_TOP_RIGHT, -30, 66);
    lv_label_set_text(s_time, "25:00");

    s_status = lv_label_create(face);
    lv_obj_set_style_text_color(s_status, lv_color_hex(AR_PINK), 0);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_16, 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_RIGHT, -30, 130);
    lv_label_set_text(s_status, "** READY **");

    /* health-bar progress */
    s_bar = lv_bar_create(face);
    lv_obj_set_size(s_bar, 400, 22);
    lv_obj_align(s_bar, LV_ALIGN_CENTER, 0, 40);
    lv_bar_set_range(s_bar, 0, 1000);
    lv_bar_set_value(s_bar, 1000, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(AR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_bar, lv_color_hex(AR_YELLOW), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(AR_RED), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, 0, LV_PART_INDICATOR);

    s_score = lv_label_create(face);
    lv_obj_set_style_text_color(s_score, lv_color_hex(AR_GREEN), 0);
    lv_obj_set_style_text_font(s_score, &lv_font_montserrat_16, 0);
    lv_obj_align(s_score, LV_ALIGN_CENTER, 0, 78);
    lv_label_set_text(s_score, "LVL 1  x0");
}

static void ar_update(const timer_view_t *v) {
    char buf[8]; unsigned s = v->rem_ms/1000;
    snprintf(buf, sizeof buf, "%02u:%02u", s/60, s%60);
    lv_label_set_text(s_time, buf);

    int32_t val = v->total_ms ? (int32_t)((uint64_t)v->rem_ms*1000/v->total_ms) : 0;
    lv_bar_set_value(s_bar, val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(v->break_phase?AR_GREEN:AR_RED), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_body, lv_color_hex(v->break_phase?AR_GREEN:AR_RED), 0);

    const char *name = v->alarm ? "** LEVEL UP! **" :
        v->paused ? "-- PAUSED --" :
        v->idle ? "** READY **" :
        (v->break_phase ? "~ BREAK ~" : "** FOCUS **");
    lv_label_set_text(s_status, name);

    char sc[28]; snprintf(sc, sizeof sc, "LVL %u  x%u", v->completed + 1, v->streak);
    lv_label_set_text(s_score, sc);
}

const theme_t THEME_ARCADE = {
    .name="Arcade", .bg=AR_BG, .accent=AR_RED, .cool=AR_GREEN,
    .build=ar_build, .update=ar_update,
};
