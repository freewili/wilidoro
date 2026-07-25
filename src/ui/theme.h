// src/ui/theme.h
#ifndef WILIDORO_THEME_H
#define WILIDORO_THEME_H
#include "lvgl.h"
#include "timer_view.h"

typedef struct {
    const char *name;
    uint32_t    bg;       /* screen background */
    uint32_t    accent;   /* focus / primary accent (also used by Settings/Nearby titles) */
    uint32_t    cool;     /* break color */
    void (*build)(lv_obj_t *face);     /* create widgets into the face container */
    void (*update)(const timer_view_t *v);  /* refresh from state (build() ran first) */
} theme_t;

const theme_t *theme_get(uint8_t idx);   /* idx 0..2 -> neon/arcade/flip; clamps out-of-range to 0 */

/* Each theme .c exposes its descriptor for the registry: */
extern const theme_t THEME_NEON;
extern const theme_t THEME_ARCADE;
extern const theme_t THEME_FLIP;
#endif
