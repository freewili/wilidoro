#define WILIDORO_SIM 1
#include "lvgl.h"
#include <SDL2/SDL.h>
#include "hal.h"
#include "app.h"
#include "ui.h"

void sim_dvi_create(void);
void sim_dvi_present(void);

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    lv_init();
    lv_sdl_window_create(UI_W, UI_H);
    lv_sdl_mouse_create();
#if !defined(WILIDORO_BOARD_OG)
    sim_dvi_create();
#endif
    hal_init();               /* SDL key watch */
    app_init();
    while (1) {
        hal_pump();
        lv_timer_handler();
#if !defined(WILIDORO_BOARD_OG)
        sim_dvi_present();
#endif
        SDL_Delay(5);
    }
    return 0;
}
