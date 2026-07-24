#include "screen_settings.h"
#include "ui.h"
#include "app.h"
static lv_obj_t *s_scr, *s_title;
lv_obj_t *screen_settings_create(void) {
    s_scr = ui_screen();
    s_title = lv_label_create(s_scr);
    lv_obj_set_style_text_color(s_title, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_48, 0);
    lv_obj_center(s_title);
    lv_label_set_text(s_title, "Settings");
    return s_scr;
}
void screen_settings_update(void) { }
void screen_settings_softkey(int col) { (void)col; }
