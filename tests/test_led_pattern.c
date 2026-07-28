#include "greatest.h"
#include "led_pattern.h"
#include <string.h>

static timer_view_t V(bool idle, bool paused, bool alarm, bool brk, uint32_t total, uint32_t rem) {
    timer_view_t v = {0};
    v.idle=idle; v.paused=paused; v.alarm=alarm; v.break_phase=brk;
    v.total_ms=total; v.rem_ms=rem;
    return v;
}
static int lit_count(const led_rgb_t *o, int n) {
    int c=0; for (int i=0;i<n;i++) if (o[i].r||o[i].g||o[i].b) c++; return c;
}

TEST idle_all_off(void) {
    led_rgb_t o[LED_COUNT_MAX];
    timer_view_t v = V(true,false,false,false, 1500000, 1500000);
    led_pattern_render(0, &v, o, 16);
    ASSERT_EQ(0, lit_count(o, 16));
    PASS();
}

TEST focus_fills_proportional(void) {
    led_rgb_t o[LED_COUNT_MAX];
    /* 50% elapsed: total=1000ms, rem=500ms -> 8 of 16 lit */
    timer_view_t v = V(false,false,false,false, 1000, 500);
    led_pattern_render(0, &v, o, 16);
    ASSERT_EQ(8, lit_count(o, 16));
    ASSERT(o[0].r > o[0].b);   /* neon focus is warm */
    PASS();
}

TEST break_uses_cool_color(void) {
    led_rgb_t o[LED_COUNT_MAX];
    timer_view_t v = V(false,false,false,true, 1000, 750);  /* 25% -> 4 of 16 */
    led_pattern_render(0, &v, o, 16);
    ASSERT_EQ(4, lit_count(o, 16));
    ASSERT(o[0].b > o[0].r);   /* neon break is cool */
    PASS();
}

TEST alarm_lights_all(void) {
    led_rgb_t o[LED_COUNT_MAX];
    timer_view_t v = V(false,false,true,false, 1000, 0);
    led_pattern_render(1, &v, o, 16);   /* arcade celebrate */
    ASSERT_EQ(16, lit_count(o, 16));
    PASS();
}

TEST paused_is_dimmer_than_running(void) {
    led_rgb_t run[LED_COUNT_MAX], pau[LED_COUNT_MAX];
    timer_view_t vr = V(false,false,false,false, 1000, 500);
    timer_view_t vp = V(false,true, false,false, 1000, 500);
    led_pattern_render(0, &vr, run, 16);
    led_pattern_render(0, &vp, pau, 16);
    ASSERT_EQ(lit_count(run,16), lit_count(pau,16));
    ASSERT(pau[0].r < run[0].r);
    PASS();
}

/* ---- OG geometry: 7 LEDs ---- */

TEST og_seven_fills_proportional(void) {
    led_rgb_t o[LED_COUNT_MAX];
    /* 50% elapsed: total=1000ms, rem=500ms -> elapsed*7/1000 = 3.5, truncates
       to 3 of 7 lit. */
    timer_view_t v = V(false,false,false,false, 1000, 500);
    led_pattern_render(0, &v, o, 7);
    ASSERT_EQ(3, lit_count(o, 7));
    PASS();
}

TEST og_seven_alarm_lights_all_seven(void) {
    led_rgb_t o[LED_COUNT_MAX];
    timer_view_t v = V(false,false,true,false, 1000, 0);
    led_pattern_render(1, &v, o, 7);
    ASSERT_EQ(7, lit_count(o, 7));
    PASS();
}

TEST og_seven_does_not_touch_slack(void) {
    led_rgb_t o[LED_COUNT_MAX];
    /* Sentinel the tail: rendering at count 7 must leave indices 7..15 alone. */
    for (int i = 0; i < LED_COUNT_MAX; i++) { o[i].r = 0xAB; o[i].g = 0xCD; o[i].b = 0xEF; }
    timer_view_t v = V(false,false,true,false, 1000, 0);   /* alarm: writes every led */
    led_pattern_render(1, &v, o, 7);
    for (int i = 7; i < LED_COUNT_MAX; i++) {
        ASSERT_EQ(0xAB, o[i].r); ASSERT_EQ(0xCD, o[i].g); ASSERT_EQ(0xEF, o[i].b);
    }
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
    RUN_TEST(og_seven_fills_proportional);
    RUN_TEST(og_seven_alarm_lights_all_seven);
    RUN_TEST(og_seven_does_not_touch_slack);
    GREATEST_MAIN_END();
}
