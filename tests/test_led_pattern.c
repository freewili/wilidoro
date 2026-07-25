#include "greatest.h"
#include "led_pattern.h"
#include <string.h>

static timer_view_t V(bool idle, bool paused, bool alarm, bool brk, uint32_t total, uint32_t rem) {
    timer_view_t v = {0};
    v.idle=idle; v.paused=paused; v.alarm=alarm; v.break_phase=brk;
    v.total_ms=total; v.rem_ms=rem;
    return v;
}
static int lit_count(const led_rgb_t *o) {
    int n=0; for (int i=0;i<LED_COUNT;i++) if (o[i].r||o[i].g||o[i].b) n++; return n;
}

TEST idle_all_off(void) {
    led_rgb_t o[LED_COUNT];
    timer_view_t v = V(true,false,false,false, 1500000, 1500000);
    led_pattern_render(0, &v, o);
    ASSERT_EQ(0, lit_count(o));
    PASS();
}

TEST focus_fills_proportional(void) {
    led_rgb_t o[LED_COUNT];
    /* 50% elapsed: total=1000ms, rem=500ms -> 8 LEDs lit */
    timer_view_t v = V(false,false,false,false, 1000, 500);
    led_pattern_render(0, &v, o);
    ASSERT_EQ(8, lit_count(o));
    /* neon focus color is warm (r dominant) */
    ASSERT(o[0].r > o[0].b);
    PASS();
}

TEST break_uses_cool_color(void) {
    led_rgb_t o[LED_COUNT];
    timer_view_t v = V(false,false,false,true, 1000, 750);  /* 25% -> 4 lit */
    led_pattern_render(0, &v, o);
    ASSERT_EQ(4, lit_count(o));
    ASSERT(o[0].b > o[0].r);   /* neon break color is cool (blue/teal dominant) */
    PASS();
}

TEST alarm_lights_all(void) {
    led_rgb_t o[LED_COUNT];
    timer_view_t v = V(false,false,true,false, 1000, 0);
    led_pattern_render(1, &v, o);   /* arcade celebrate */
    ASSERT_EQ(16, lit_count(o));
    PASS();
}

TEST paused_is_dimmer_than_running(void) {
    led_rgb_t run[LED_COUNT], pau[LED_COUNT];
    timer_view_t vr = V(false,false,false,false, 1000, 500);
    timer_view_t vp = V(false,true, false,false, 1000, 500);
    led_pattern_render(0, &vr, run);
    led_pattern_render(0, &vp, pau);
    ASSERT_EQ(lit_count(run), lit_count(pau));   /* same count */
    ASSERT(pau[0].r < run[0].r);                 /* but dimmer */
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(idle_all_off);
    RUN_TEST(focus_fills_proportional);
    RUN_TEST(break_uses_cool_color);
    RUN_TEST(alarm_lights_all);
    RUN_TEST(paused_is_dimmer_than_running);
    GREATEST_MAIN_END();
}
