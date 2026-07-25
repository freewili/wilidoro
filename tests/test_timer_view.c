#include "greatest.h"
#include "timer_view.h"

static pm_config_t CFG = { .focus_min=25, .short_min=5, .long_min=15, .long_every=4 };
#define MIN(n) ((uint32_t)(n)*60000u)

TEST idle_previews_focus_full_ring(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    timer_view_t v = timer_view_make(&p, false, 0);
    ASSERT(v.idle);
    ASSERT_EQ(MIN(25), v.total_ms);
    ASSERT_EQ(MIN(25), v.rem_ms);       /* full ring */
    ASSERT_FALSE(v.break_phase);
    PASS();
}

TEST focus_uses_focus_total(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    pomodoro_start_focus(&p, 0);
    timer_view_t v = timer_view_make(&p, false, MIN(10));
    ASSERT_EQ(PM_FOCUS, v.eff_state);
    ASSERT_EQ(MIN(25), v.total_ms);
    ASSERT_EQ(MIN(15), v.rem_ms);
    ASSERT_FALSE(v.break_phase);
    PASS();
}

TEST paused_break_uses_break_total_and_flag(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    pomodoro_start_focus(&p, 0);
    pomodoro_tick(&p, MIN(25));                 /* -> ALARM */
    pomodoro_acknowledge(&p, MIN(25));          /* -> BREAK_SHORT */
    pomodoro_pause(&p, MIN(25)+MIN(2));         /* pause the break */
    timer_view_t v = timer_view_make(&p, false, MIN(999));
    ASSERT(v.paused);
    ASSERT_EQ(PM_BREAK_SHORT, v.eff_state);     /* effective state, not PAUSED */
    ASSERT_EQ(MIN(5), v.total_ms);              /* break total, not focus */
    ASSERT(v.break_phase);                      /* cool color */
    ASSERT_EQ(MIN(3), v.rem_ms);                /* 5 - 2 paused */
    PASS();
}

TEST session_index_wraps_by_long_every(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    p.stats.completed = 6;                      /* 6 % 4 == 2 */
    timer_view_t v = timer_view_make(&p, false, 0);
    ASSERT_EQ(2u, v.session_idx);
    ASSERT_EQ(4u, v.session_n);
    PASS();
}

TEST alarm_flag_set(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    pomodoro_start_focus(&p, 0);
    pomodoro_tick(&p, MIN(25));                 /* ALARM */
    timer_view_t v = timer_view_make(&p, true, MIN(25));
    ASSERT(v.alarm);
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(idle_previews_focus_full_ring);
    RUN_TEST(focus_uses_focus_total);
    RUN_TEST(paused_break_uses_break_total_and_flag);
    RUN_TEST(session_index_wraps_by_long_every);
    RUN_TEST(alarm_flag_set);
    GREATEST_MAIN_END();
}
