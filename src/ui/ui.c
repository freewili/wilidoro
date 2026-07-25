#include "ui.h"
#include <stdint.h>
#include "app.h"

static const uint32_t SK_COLORS[5] = {0x9AA6B2,0xE7C64B,0x39B36B,0x3B7DE0,0xD8503C};

static void sk_event(lv_event_t *e) {
    ui_softkey_cb_t cb = (ui_softkey_cb_t)lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target_obj(e);
    int col = (int)(intptr_t)lv_obj_get_user_data(btn);
    app_sound(SND_BLIP);
    if (cb) cb(col);
}
lv_obj_t *ui_screen(void) {
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_pad_all(s, 0, 0);
    return s;
}
lv_obj_t *ui_softkey_bar(lv_obj_t *parent, ui_softkey_cb_t cb) {
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, 480, 34);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0D1420), 0);
    lv_obj_set_style_pad_all(bar, 2, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int i = 0; i < 5; i++) {
        lv_obj_t *btn = lv_button_create(bar);
        lv_obj_set_size(btn, 92, 28);
        lv_obj_set_style_bg_color(btn, lv_color_hex(SK_COLORS[i]), 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_add_event_cb(btn, sk_event, LV_EVENT_CLICKED, (void*)cb);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, "");
        lv_obj_center(lbl);
    }
    return bar;
}
void ui_softkey_set_labels(lv_obj_t *bar, const char *labels[5]) {
    for (int i = 0; i < 5; i++) {
        lv_obj_t *btn = lv_obj_get_child(bar, i);
        lv_obj_t *lbl = lv_obj_get_child(btn, 0);
        lv_label_set_text(lbl, labels[i] ? labels[i] : "");
        lv_obj_set_style_bg_opa(btn, labels[i] ? LV_OPA_COVER : LV_OPA_30, 0);
    }
}
