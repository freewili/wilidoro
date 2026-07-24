#define WILIDORO_SIM 1
#include "lvgl.h"
#include <SDL2/SDL.h>
#include "hal.h"
#include "app.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    lv_init();
    lv_sdl_window_create(480, 320);
    lv_sdl_mouse_create();
    hal_init();               /* SDL key watch */
    app_init();
    while (1) { hal_pump(); lv_timer_handler(); SDL_Delay(5); }
    return 0;
}
