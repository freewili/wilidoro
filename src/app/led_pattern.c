#include "led_pattern.h"
#include <string.h>

typedef struct { led_rgb_t focus, brk, celebrate; } palette_t;
static const palette_t PALETTES[3] = {
    /* neon   */ { {255,91,69},  {45,212,191}, {255,150,80} },
    /* arcade */ { {255,71,87},  {46,213,115}, {255,224,102} },
    /* flip   */ { {200,120,60}, {138,125,107},{240,200,120} },
};

void led_pattern_render(uint8_t theme_idx, const timer_view_t *v, led_rgb_t out[LED_COUNT]) {
    const palette_t *p = &PALETTES[theme_idx < 3 ? theme_idx : 0];
    memset(out, 0, sizeof(led_rgb_t) * LED_COUNT);
    if (v->idle) return;
    if (v->alarm) {
        for (int i = 0; i < LED_COUNT; i++) out[i] = p->celebrate;
        return;
    }
    uint32_t elapsed = v->total_ms ? (uint32_t)((uint64_t)(v->total_ms - v->rem_ms) * 1000 / v->total_ms) : 0;
    int lit = (int)(elapsed * LED_COUNT / 1000);
    if (lit > LED_COUNT) lit = LED_COUNT;
    led_rgb_t c = v->break_phase ? p->brk : p->focus;
    if (v->paused) { c.r >>= 1; c.g >>= 1; c.b >>= 1; }
    for (int i = 0; i < lit; i++) out[i] = c;
}
