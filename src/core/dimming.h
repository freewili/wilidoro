// src/core/dimming.h
#ifndef WILIDORO_DIMMING_H
#define WILIDORO_DIMMING_H
#include <stdint.h>

#define DIM_MIN_PCT   40.0f
#define DIM_FULL_LUX  400.0f
#define DIM_ALPHA     0.25f
#define DIM_LED_FLOOR 8      /* min LED brightness 0..255 */

typedef struct { float smooth; } dim_state_t;

void    dim_init(dim_state_t *d, float initial_pct);
uint8_t dim_apply(dim_state_t *d, float lux);        /* -> backlight 0..100, smoothed */
uint8_t dim_led_brightness(uint8_t backlight_pct);   /* -> WS2812 brightness 0..255 */
#endif
