// src/app/led_pattern.h
#ifndef WILIDORO_LED_PATTERN_H
#define WILIDORO_LED_PATTERN_H
#include <stdint.h>
#include "timer_view.h"

#define LED_COUNT 16
typedef struct { uint8_t r, g, b; } led_rgb_t;

/* Render the 16-LED array for the given theme (0=neon,1=arcade,2=flip) and timer state.
   Sets full 0..255 colors; brightness is applied separately by the driver. */
void led_pattern_render(uint8_t theme_idx, const timer_view_t *v, led_rgb_t out[LED_COUNT]);
#endif
