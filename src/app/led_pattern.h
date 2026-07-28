// src/app/led_pattern.h
#ifndef WILIDORO_LED_PATTERN_H
#define WILIDORO_LED_PATTERN_H
#include <stdint.h>
#include "timer_view.h"

/* Largest ring any supported board has: FreeWili 2 has 16, the OG has 7.
   Callers size stack arrays with this and pass the live count explicitly, so
   the renderer is board-agnostic and the host tests can cover both. */
#define LED_COUNT_MAX 16
typedef struct { uint8_t r, g, b; } led_rgb_t;

/* Render `count` LEDs for the given theme (0=neon,1=arcade,2=flip) and timer
   state. Writes exactly out[0..count-1] and never touches out[count..].
   Sets full 0..255 colors; brightness is applied separately by the driver. */
void led_pattern_render(uint8_t theme_idx, const timer_view_t *v, led_rgb_t *out, int count);
#endif
