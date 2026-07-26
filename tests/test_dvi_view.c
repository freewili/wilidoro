#include "greatest.h"
#include "dvi_view.h"
#include <string.h>

/* Surfaces are deliberately STRIDED: 120 uint16 of slack per row stands in for
   the HSTX command words that live between rows on real hardware. */
#define TSTRIDE (DVI_VIEW_W + 120)
#define SENTINEL 0xBEEF

static uint16_t g_buf[DVI_VIEW_H * TSTRIDE];
static uint16_t g_buf2[DVI_VIEW_H * TSTRIDE];

static dvi_surface_t surf(uint16_t *b) {
    for (int i = 0; i < DVI_VIEW_H * TSTRIDE; i++) b[i] = SENTINEL;
    dvi_surface_t s = { b, TSTRIDE, DVI_VIEW_W, DVI_VIEW_H };
    return s;
}
static uint16_t px(const uint16_t *b, int x, int y) { return b[(size_t)y * TSTRIDE + x]; }

/* Every out-of-bounds column of every row must still hold the sentinel. */
static int slack_intact(const uint16_t *b) {
    for (int y = 0; y < DVI_VIEW_H; y++)
        for (int x = DVI_VIEW_W; x < TSTRIDE; x++)
            if (px(b, x, y) != SENTINEL) return 0;
    return 1;
}

static timer_view_t mk(bool idle, bool paused, bool alarm, bool brk,
                       uint32_t rem_ms, unsigned idx, unsigned n) {
    timer_view_t v;
    memset(&v, 0, sizeof v);
    v.idle = idle; v.paused = paused; v.alarm = alarm; v.break_phase = brk;
    v.rem_ms = rem_ms; v.total_ms = 25u*60u*1000u;
    v.session_idx = idx; v.session_n = n;
    return v;
}

/* ---- THE critical test: never write into the command words ---- */
TEST render_never_writes_past_width(void) {
    dvi_surface_t s = surf(g_buf);
    timer_view_t v = mk(false, false, false, false, 25u*60u*1000u, 1, 4);
    dvi_view_render(0, &v, &s);
    ASSERT(slack_intact(g_buf));
    PASS();
}

TEST every_state_and_theme_stays_in_bounds(void) {
    timer_view_t states[5] = {
        mk(true,  false, false, false, 25u*60u*1000u, 0, 4),
        mk(false, false, false, false, 90u*1000u,     1, 4),
        mk(false, true,  false, false, 61u*1000u,     2, 4),
        mk(false, false, true,  false, 0,             3, 4),
        mk(false, false, false, true,  5u*60u*1000u,  4, 4),
    };
    for (int t = 0; t < 4; t++) {          /* includes out-of-range theme 3 */
        for (int i = 0; i < 5; i++) {
            dvi_surface_t s = surf(g_buf);
            dvi_view_render((uint8_t)t, &states[i], &s);
            ASSERT(slack_intact(g_buf));
        }
    }
    PASS();
}

TEST background_is_painted(void) {
    dvi_surface_t s = surf(g_buf);
    timer_view_t v = mk(false, false, false, false, 25u*60u*1000u, 1, 4);
    dvi_view_render(0, &v, &s);
    /* top-left corner is background, never a glyph */
    ASSERT(px(g_buf, 0, 0) != SENTINEL);
    ASSERT_EQ(px(g_buf, 0, 0), px(g_buf, DVI_VIEW_W - 1, 0));
    PASS();
}

/* 08:08 -- the tens-of-minutes digit is '0' (no middle segment), the units
   digit is '8' (has one). Middle segment sits at the digit's vertical centre. */
TEST seven_segment_digits_are_correct(void) {
    dvi_surface_t s = surf(g_buf);
    timer_view_t v = mk(false, false, false, false, (8u*60u + 8u) * 1000u, 1, 4);
    dvi_view_render(0, &v, &s);
    uint16_t bg = px(g_buf, 0, 0);
    const int y_mid = 68 + 120 / 2;          /* DIG_Y + DIG_H/2 */
    const int x_d0  = 36 + 84 / 2;           /* centre of digit 0 */
    const int x_d1  = 36 + 84 + 12 + 84 / 2; /* centre of digit 1 */
    ASSERT_EQ(bg, px(g_buf, x_d0, y_mid));   /* '0' has no middle bar */
    ASSERT(px(g_buf, x_d1, y_mid) != bg);    /* '8' does */
    PASS();
}

TEST themes_use_different_palettes(void) {
    timer_view_t v = mk(false, false, false, false, 25u*60u*1000u, 1, 4);
    dvi_surface_t a = surf(g_buf);  dvi_view_render(0, &v, &a);
    dvi_surface_t b = surf(g_buf2); dvi_view_render(1, &v, &b);
    ASSERT(memcmp(g_buf, g_buf2, sizeof g_buf) != 0);
    PASS();
}

TEST out_of_range_theme_clamps_to_zero(void) {
    timer_view_t v = mk(false, false, false, false, 25u*60u*1000u, 1, 4);
    dvi_surface_t a = surf(g_buf);  dvi_view_render(0, &v, &a);
    dvi_surface_t b = surf(g_buf2); dvi_view_render(9, &v, &b);
    ASSERT_EQ(0, memcmp(g_buf, g_buf2, sizeof g_buf));
    PASS();
}

/* Confounder guard: break_phase also flips the state WORD (FOCUS vs BREAK), so
   a whole-buffer memcmp would pass even if the foreground-colour selection were
   hardcoded. Sample a pixel inside a digit stroke instead -- 08:08's units-of-
   minutes digit ('8') has a middle segment at its vertical/horizontal centre --
   and assert that specific pixel's colour actually differs between the two
   renders (same digit-centre math as seven_segment_digits_are_correct). */
TEST break_phase_recolors(void) {
    timer_view_t f = mk(false, false, false, false, (8u*60u + 8u) * 1000u, 1, 4);
    timer_view_t b = mk(false, false, false, true,  (8u*60u + 8u) * 1000u, 1, 4);
    dvi_surface_t sa = surf(g_buf);  dvi_view_render(0, &f, &sa);
    dvi_surface_t sb = surf(g_buf2); dvi_view_render(0, &b, &sb);
    ASSERT(memcmp(g_buf, g_buf2, sizeof g_buf) != 0);
    const int y_mid = 68 + 120 / 2;          /* DIG_Y + DIG_H/2 */
    const int x_d1  = 36 + 84 + 12 + 84 / 2; /* centre of units-of-minutes digit */
    ASSERT(px(g_buf, x_d1, y_mid) != px(g_buf2, x_d1, y_mid));
    PASS();
}

TEST state_word_changes_the_image(void) {
    timer_view_t i = mk(true,  false, false, false, 25u*60u*1000u, 0, 4);
    timer_view_t p = mk(false, true,  false, false, 25u*60u*1000u, 0, 4);
    dvi_surface_t sa = surf(g_buf);  dvi_view_render(0, &i, &sa);
    dvi_surface_t sb = surf(g_buf2); dvi_view_render(0, &p, &sb);
    ASSERT(memcmp(g_buf, g_buf2, sizeof g_buf) != 0);
    PASS();
}

/* ---- dirty tracking: each case gets its OWN tracker, proving no leakage ---- */
TEST dirty_is_true_on_a_fresh_tracker(void) {
    dvi_dirty_t d; dvi_dirty_reset(&d);
    timer_view_t v = mk(false, false, false, false, 60000, 1, 4);
    ASSERT(dvi_view_dirty(&d, 0, &v));
    PASS();
}

TEST dirty_is_false_within_the_same_second(void) {
    dvi_dirty_t d; dvi_dirty_reset(&d);
    timer_view_t v = mk(false, false, false, false, 60000, 1, 4);
    ASSERT(dvi_view_dirty(&d, 0, &v));
    timer_view_t v2 = mk(false, false, false, false, 59800, 1, 4);
    ASSERT_FALSE(dvi_view_dirty(&d, 0, &v2));   /* still 60 s when rounded up */
    PASS();
}

TEST dirty_is_true_across_a_second_theme_or_state(void) {
    timer_view_t v = mk(false, false, false, false, 60000, 1, 4);
    dvi_dirty_t a; dvi_dirty_reset(&a);
    ASSERT(dvi_view_dirty(&a, 0, &v));
    timer_view_t later = mk(false, false, false, false, 58000, 1, 4);
    ASSERT(dvi_view_dirty(&a, 0, &later));

    dvi_dirty_t b; dvi_dirty_reset(&b);
    ASSERT(dvi_view_dirty(&b, 0, &v));
    ASSERT(dvi_view_dirty(&b, 1, &v));          /* theme changed */

    dvi_dirty_t c; dvi_dirty_reset(&c);
    ASSERT(dvi_view_dirty(&c, 0, &v));
    timer_view_t paused = mk(false, true, false, false, 60000, 1, 4);
    ASSERT(dvi_view_dirty(&c, 0, &paused));     /* state changed */
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(render_never_writes_past_width);
    RUN_TEST(every_state_and_theme_stays_in_bounds);
    RUN_TEST(background_is_painted);
    RUN_TEST(seven_segment_digits_are_correct);
    RUN_TEST(themes_use_different_palettes);
    RUN_TEST(out_of_range_theme_clamps_to_zero);
    RUN_TEST(break_phase_recolors);
    RUN_TEST(state_word_changes_the_image);
    RUN_TEST(dirty_is_true_on_a_fresh_tracker);
    RUN_TEST(dirty_is_false_within_the_same_second);
    RUN_TEST(dirty_is_true_across_a_second_theme_or_state);
    GREATEST_MAIN_END();
}
