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
    /* 50% elapsed: total=1000ms, rem=500ms -> the bar sits at 3.5 of 7 LEDs.
       LEDs 0-2 are now fully lit AND LED 3 is lit at partial (~50%)
       intensity instead of staying dark -- the whole point of the smooth
       scaling change -- so 4 pixels read non-zero, not 3. */
    timer_view_t v = V(false,false,false,false, 1000, 500);
    led_pattern_render(0, &v, o, 7);
    ASSERT_EQ(4, lit_count(o, 7));
    PASS();
}

/* ---- Smooth (fractional) leading-LED intensity ---- */

TEST leading_led_scales_with_fraction_seven(void) {
    led_rgb_t o[LED_COUNT_MAX];
    /* 3.5 of 7 LEDs: 0-2 full, 3 partial (~50%), 4-6 off. */
    timer_view_t v = V(false,false,false,false, 1000, 500);
    led_pattern_render(0, &v, o, 7);
    ASSERT(o[0].r == o[1].r && o[1].r == o[2].r);   /* behind the edge: full, equal */
    ASSERT(o[0].r > 0);
    ASSERT(o[3].r > 0 && o[3].r < o[0].r);          /* leading edge: dim, not off, not full */
    for (int i = 4; i < 7; i++) {
        ASSERT_EQ(0, o[i].r); ASSERT_EQ(0, o[i].g); ASSERT_EQ(0, o[i].b);
    }
    PASS();
}

TEST leading_led_scales_with_fraction_sixteen(void) {
    led_rgb_t o[LED_COUNT_MAX];
    /* total=1000, rem=469 -> elapsed=531/1000 -> 531*16/1000 = 8.496 LEDs:
       0-7 full, 8 partial (~49.6%), 9-15 off. */
    timer_view_t v = V(false,false,false,false, 1000, 469);
    led_pattern_render(0, &v, o, 16);
    for (int i = 1; i < 8; i++) ASSERT_EQ(o[0].r, o[i].r);   /* behind the edge: full, equal */
    ASSERT(o[0].r > 0);
    ASSERT(o[8].r > 0 && o[8].r < o[0].r);                   /* leading edge: dim, not off/full */
    for (int i = 9; i < 16; i++) {
        ASSERT_EQ(0, o[i].r); ASSERT_EQ(0, o[i].g); ASSERT_EQ(0, o[i].b);
    }
    PASS();
}

TEST zero_percent_lights_nothing_no_partial(void) {
    led_rgb_t o[LED_COUNT_MAX];
    /* rem == total -> 0% elapsed. Not idle, so this exercises the progress
       path itself rather than the early idle return -- no partial pixel
       must appear at this endpoint either. */
    timer_view_t v16 = V(false,false,false,false, 1000, 1000);
    led_pattern_render(0, &v16, o, 16);
    ASSERT_EQ(0, lit_count(o, 16));
    timer_view_t v7 = V(false,false,false,false, 1000, 1000);
    led_pattern_render(0, &v7, o, 7);
    ASSERT_EQ(0, lit_count(o, 7));
    PASS();
}

TEST hundred_percent_lights_all_full_no_half_lit(void) {
    led_rgb_t o[LED_COUNT_MAX];
    /* rem == 0 -> 100% elapsed. Every LED must be at full, equal intensity;
       none dimmed as a "leading" pixel at this endpoint. */
    timer_view_t v16 = V(false,false,false,false, 1000, 0);
    led_pattern_render(0, &v16, o, 16);
    ASSERT(o[0].r > 0);
    for (int i = 1; i < 16; i++) ASSERT_EQ(o[0].r, o[i].r);
    timer_view_t v7 = V(false,false,false,false, 1000, 0);
    led_pattern_render(0, &v7, o, 7);
    ASSERT(o[0].r > 0);
    for (int i = 1; i < 7; i++) ASSERT_EQ(o[0].r, o[i].r);
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
    /* Sentinel the tail: rendering at count 7 must leave indices 7..15 alone.
       Must be a progress state, not alarm: alarm returns at led_pattern.c:17
       -19, before the fractional leading-LED code ever runs, so an alarm
       state here would never exercise out[lit_full] -- the one write the
       smooth-intensity change added -- against this sentinel at all. 50%
       elapsed (total=1000, rem=500) puts the leading edge mid-array
       (lit_full=3 of 7), so out[3] actually gets written and checked. */
    for (int i = 0; i < LED_COUNT_MAX; i++) { o[i].r = 0xAB; o[i].g = 0xCD; o[i].b = 0xEF; }
    timer_view_t v = V(false,false,false,false, 1000, 500);
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
    RUN_TEST(leading_led_scales_with_fraction_seven);
    RUN_TEST(leading_led_scales_with_fraction_sixteen);
    RUN_TEST(zero_percent_lights_nothing_no_partial);
    RUN_TEST(hundred_percent_lights_all_full_no_half_lit);
    RUN_TEST(og_seven_alarm_lights_all_seven);
    RUN_TEST(og_seven_does_not_touch_slack);
    GREATEST_MAIN_END();
}
