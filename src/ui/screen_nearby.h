#ifndef WILIDORO_SCREEN_NEARBY_H
#define WILIDORO_SCREEN_NEARBY_H
#include "lvgl.h"
lv_obj_t *screen_nearby_create(void);
void      screen_nearby_update(void);
void      screen_nearby_softkey(int col);
#endif
