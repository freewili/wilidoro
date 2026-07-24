#define WILIDORO_SIM 1
#include "lvgl.h"
#include <SDL2/SDL.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    lv_init();
    lv_display_t *disp = lv_sdl_window_create(480, 320);
    lv_indev_t *mouse = lv_sdl_mouse_create();
    (void)mouse; (void)disp;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0C0C12), LV_PART_MAIN);
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "wilidoro (sim)");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFF5B45), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(label);

    while (1) {
        lv_timer_handler();
        SDL_Delay(5);
    }
    return 0;
}
