#include "screen_timer.h"
#include "ui.h"
#include "app.h"
#include "pomodoro.h"
static lv_obj_t *s_scr, *s_time;
lv_obj_t *screen_timer_create(void) {
    s_scr = ui_screen();
    s_time = lv_label_create(s_scr);
    lv_obj_set_style_text_color(s_time, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_text_font(s_time, &lv_font_montserrat_48, 0);
    lv_obj_center(s_time);
    lv_label_set_text(s_time, "25:00");
    return s_scr;
}
void screen_timer_update(void) {
    uint32_t ms = pomodoro_remaining_ms(&app()->pomo, 0); /* replaced with hal now in Task 6 */
    (void)ms;
}
void screen_timer_softkey(int col) { (void)col; }
