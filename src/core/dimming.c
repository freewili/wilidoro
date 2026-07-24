#include "dimming.h"

void dim_init(dim_state_t *d, float initial_pct) { d->smooth = initial_pct; }

uint8_t dim_apply(dim_state_t *d, float lux) {
    float target = DIM_MIN_PCT + (lux / DIM_FULL_LUX) * (100.0f - DIM_MIN_PCT);
    if (target > 100.0f) target = 100.0f;
    if (target < DIM_MIN_PCT) target = DIM_MIN_PCT;
    d->smooth += (target - d->smooth) * DIM_ALPHA;
    if (d->smooth > 100.0f) d->smooth = 100.0f;
    if (d->smooth < DIM_MIN_PCT) d->smooth = DIM_MIN_PCT;
    return (uint8_t)(d->smooth + 0.5f);
}

uint8_t dim_led_brightness(uint8_t backlight_pct) {
    if (backlight_pct > 100) backlight_pct = 100;
    int b = DIM_LED_FLOOR + (255 - DIM_LED_FLOOR) * backlight_pct / 100;
    return (uint8_t)b;
}
