#include "theme.h"
#include <stdio.h>

#define NEON_BG     0x0C0C12
#define NEON_PANEL  0x141b26
#define NEON_TEXT   0xDDE6F2
#define NEON_MUTED  0x6B7C93
#define NEON_ACCENT 0xFF5B45
#define NEON_COOL   0x2DD4BF

static lv_obj_t *s_arc, *s_time, *s_state;

static void neon_build(lv_obj_t *face) {
    s_arc = lv_arc_create(face);
    lv_obj_set_size(s_arc, 220, 220);
    lv_obj_align(s_arc, LV_ALIGN_CENTER, 0, -6);
    lv_arc_set_rotation(s_arc, 270);
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_arc_set_range(s_arc, 0, 1000);
    lv_arc_set_value(s_arc, 1000);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_arc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(NEON_PANEL), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(NEON_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_INDICATOR);

    s_time = lv_label_create(face);
    lv_obj_set_style_text_color(s_time, lv_color_hex(NEON_TEXT), 0);
    lv_obj_set_style_text_font(s_time, &lv_font_montserrat_48, 0);
    lv_obj_align(s_time, LV_ALIGN_CENTER, 0, -16);
    lv_label_set_text(s_time, "25:00");

    s_state = lv_label_create(face);
    lv_obj_set_style_text_color(s_state, lv_color_hex(NEON_MUTED), 0);
    lv_obj_set_style_text_font(s_state, &lv_font_montserrat_16, 0);
    lv_obj_align(s_state, LV_ALIGN_CENTER, 0, 26);
    lv_label_set_text(s_state, "READY");
}

static void neon_update(const timer_view_t *v) {
    char buf[8]; unsigned s = v->rem_ms/1000;
    snprintf(buf, sizeof buf, "%02u:%02u", s/60, s%60);
    lv_label_set_text(s_time, buf);

    int32_t val = v->total_ms ? (int32_t)((uint64_t)v->rem_ms*1000/v->total_ms) : 0;
    lv_arc_set_value(s_arc, val);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(v->break_phase?NEON_COOL:NEON_ACCENT), LV_PART_INDICATOR);

    const char *name = v->alarm ? "TIME'S UP" :
        v->paused ? "PAUSED" :
        v->idle ? "READY" :
        (v->break_phase ? "BREAK" : "FOCUS");
    char st[24]; snprintf(st, sizeof st, "%s  %u/%u", name, v->session_idx, v->session_n);
    lv_label_set_text(s_state, st);
}

const theme_t THEME_NEON = {
    .name="Neon Arc", .bg=NEON_BG, .accent=NEON_ACCENT, .cool=NEON_COOL,
    .build=neon_build, .update=neon_update,
};
