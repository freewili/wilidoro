#include "led_pattern.h"
#include <string.h>

typedef struct { led_rgb_t focus, brk, celebrate; } palette_t;
static const palette_t PALETTES[3] = {
    /* neon   */ { {255,91,69},  {45,212,191}, {255,150,80} },
    /* arcade */ { {255,71,87},  {46,213,115}, {255,224,102} },
    /* flip   */ { {200,120,60}, {138,125,107},{240,200,120} },
};

void led_pattern_render(uint8_t theme_idx, const timer_view_t *v, led_rgb_t *out, int count) {
    if (count <= 0) return;
    if (count > LED_COUNT_MAX) count = LED_COUNT_MAX;
    const palette_t *p = &PALETTES[theme_idx < 3 ? theme_idx : 0];
    memset(out, 0, sizeof(led_rgb_t) * (size_t)count);
    if (v->idle) return;
    if (v->alarm) {
        for (int i = 0; i < count; i++) out[i] = p->celebrate;
        return;
    }
    uint32_t elapsed = v->total_ms ? (uint32_t)((uint64_t)(v->total_ms - v->rem_ms) * 1000 / v->total_ms) : 0;
    if (elapsed > 1000u) elapsed = 1000u;   /* safety clamp: rem_ms should never exceed total_ms */

    /* numerator is elapsed(0..1000) * count, so it maxes out at 1000*count --
       well inside uint32_t for any LED count this firmware will ever see.
       lit_full is how many whole LEDs are behind the leading edge; frac is
       the leading LED's intensity in thousandths (0..999). frac is 0 only
       exactly on a LED boundary, which is why neither 0% nor 100% ever
       half-lights a pixel -- see zero_percent_lights_nothing_no_partial and
       hundred_percent_lights_all_full_no_half_lit in tests/test_led_pattern.c. */
    uint32_t numerator = elapsed * (uint32_t)count;
    int lit_full = (int)(numerator / 1000u);
    uint32_t frac = numerator % 1000u;
    if (lit_full >= count) { lit_full = count; frac = 0u; }

    led_rgb_t c = v->break_phase ? p->brk : p->focus;
    if (v->paused) { c.r >>= 1; c.g >>= 1; c.b >>= 1; }

    for (int i = 0; i < lit_full; i++) out[i] = c;

    /* Leading (partial) LED: scale intensity by the fractional part of the
       progress instead of leaving it either fully on or fully off, so the
       bar advances smoothly rather than jumping a whole LED at a time
       (1/7th of a session on the OG, 1/16th on the FW2). Fully-lit LEDs
       behind this one (the loop above) stay at full brightness. */
    if (frac > 0u) {
        led_rgb_t partial;
        partial.r = (uint8_t)(((uint32_t)c.r * frac) / 1000u);
        partial.g = (uint8_t)(((uint32_t)c.g * frac) / 1000u);
        partial.b = (uint8_t)(((uint32_t)c.b * frac) / 1000u);
        out[lit_full] = partial;
    }
}
