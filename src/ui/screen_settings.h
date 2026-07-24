#ifndef WILIDORO_SCREEN_SETTINGS_H
#define WILIDORO_SCREEN_SETTINGS_H
#include "lvgl.h"
lv_obj_t *screen_settings_create(void);
void      screen_settings_update(void);
void      screen_settings_softkey(int col);
#endif
