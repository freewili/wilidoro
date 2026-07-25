#ifndef WILIDORO_SCREEN_TIMER_H
#define WILIDORO_SCREEN_TIMER_H
#include "lvgl.h"
lv_obj_t *screen_timer_create(void);
void      screen_timer_update(void);
void      screen_timer_softkey(int col);
void      screen_timer_apply_theme(void);   /* rebuild the face from app()->settings.theme */
#endif
