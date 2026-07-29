#include "screen_nearby.h"
#include "ui.h"
#include "app.h"
#include "app_model.h"
#include "theme.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_scr, *s_list, *s_bar, *s_empty, *s_title;

lv_obj_t *screen_nearby_create(void) {
    s_scr = ui_screen();
    s_title = lv_label_create(s_scr); lv_label_set_text(s_title,"NEARBY");
    lv_obj_set_style_text_color(s_title, lv_color_hex(UI_MUTED),0);
    lv_obj_align(s_title, LV_ALIGN_TOP_LEFT, 12, 8);

    s_list = lv_obj_create(s_scr);
    lv_obj_set_size(s_list, UI_LIST_W, UI_LIST_H);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(UI_BG), 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);

    s_empty = lv_label_create(s_scr);
    lv_obj_set_style_text_color(s_empty, lv_color_hex(UI_MUTED), 0);
    lv_label_set_text(s_empty, "no devices heard");
    lv_obj_center(s_empty);

    s_bar = ui_softkey_bar(s_scr, screen_nearby_softkey);
    const char *lbl[5] = {"Back",0,0,0,0};
    ui_softkey_set_labels(s_bar, lbl);
    return s_scr;
}

void screen_nearby_update(void) {
    lv_obj_set_style_text_color(s_title, lv_color_hex(theme_get(app()->settings.theme)->accent), 0);
    /* rebuild the list from the neighbor table each tick */
    lv_obj_clean(s_list);
    neighbor_table_t *t = &app()->neighbors;
    int shown = 0;
    for (int i = 0; i < NEIGHBOR_MAX; i++) {
        if (!t->items[i].used) continue;
        neighbor_t *n = &t->items[i];
        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_set_size(row, UI_ROW_W, UI_NEARBY_ROW_H);
        lv_obj_set_style_bg_color(row, lv_color_hex(UI_PANEL), 0);
        char nm[APP_NAME_LEN+1]; memcpy(nm, n->name, APP_NAME_LEN); nm[APP_NAME_LEN]=0;
        for (int k=APP_NAME_LEN-1;k>=0 && nm[k]==' ';k--) nm[k]=0;
        lv_obj_t *name = lv_label_create(row); lv_label_set_text(name, nm);
        lv_obj_set_style_text_color(name, lv_color_hex(UI_TEXT), 0);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 6, 4);
        const char *stname = n->state==BST_FOCUS?"focusing":(n->state==BST_BREAK?"on break":"idle");
        char sub[40];
        if (n->state==BST_IDLE) snprintf(sub,sizeof sub,"idle - %u done today", n->completed);
        else snprintf(sub,sizeof sub,"%s - %u min left", stname, n->minutes_left);
        lv_obj_t *s = lv_label_create(row); lv_label_set_text(s, sub);
        lv_obj_set_style_text_color(s, lv_color_hex(UI_MUTED), 0);
        lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 6, -4);
        shown++;
    }
    if (shown) lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
    else       lv_obj_remove_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
}

void screen_nearby_softkey(int col) { if (col==0) app_goto(SCREEN_TIMER); }
