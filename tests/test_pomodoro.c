#include "greatest.h"
#include "pomodoro.h"

static pm_config_t CFG = { .focus_min = 25, .short_min = 5, .long_min = 15, .long_every = 4 };
#define MIN(n) ((uint32_t)(n) * 60000u)

TEST starts_idle(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    ASSERT_EQ(PM_IDLE, p.state);
    ASSERT_EQ(0, p.stats.completed);
    PASS();
}

TEST focus_runs_then_alarms(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    pomodoro_start_focus(&p, 0);
    ASSERT_EQ(PM_FOCUS, p.state);
    ASSERT_EQ(MIN(25), pomodoro_remaining_ms(&p, 0));
    ASSERT_EQ(PM_EV_NONE, pomodoro_tick(&p, MIN(25) - 1));
    ASSERT_EQ(PM_EV_FOCUS_ENDED, pomodoro_tick(&p, MIN(25)));
    ASSERT_EQ(PM_ALARM, p.state);
    ASSERT_EQ(1, p.stats.completed);
    ASSERT_EQ(1, p.stats.streak);
    PASS();
}

TEST ack_goes_to_short_break(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    pomodoro_start_focus(&p, 0);
    pomodoro_tick(&p, MIN(25));
    pomodoro_acknowledge(&p, MIN(25));
    ASSERT_EQ(PM_BREAK_SHORT, p.state);
    ASSERT_EQ(MIN(5), pomodoro_remaining_ms(&p, MIN(25)));
    PASS();
}

TEST fourth_focus_earns_long_break(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    uint32_t t = 0;
    for (int i = 0; i < 3; i++) {
        pomodoro_start_focus(&p, t); t += MIN(25);
        pomodoro_tick(&p, t); pomodoro_acknowledge(&p, t);
        t += MIN(5); pomodoro_tick(&p, t);   // break ends -> idle
    }
    pomodoro_start_focus(&p, t); t += MIN(25);
    pomodoro_tick(&p, t); pomodoro_acknowledge(&p, t);
    ASSERT_EQ(PM_BREAK_LONG, p.state);
    ASSERT_EQ(4, p.stats.completed);
    PASS();
}

TEST break_end_returns_to_idle(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    pomodoro_start_focus(&p, 0);
    pomodoro_tick(&p, MIN(25)); pomodoro_acknowledge(&p, MIN(25));
    ASSERT_EQ(PM_EV_BREAK_ENDED, pomodoro_tick(&p, MIN(30)));
    ASSERT_EQ(PM_IDLE, p.state);
    PASS();
}

TEST pause_resume_preserves_remaining(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    pomodoro_start_focus(&p, 0);
    pomodoro_pause(&p, MIN(10));
    ASSERT_EQ(PM_PAUSED, p.state);
    ASSERT_EQ(MIN(15), pomodoro_remaining_ms(&p, MIN(999)));
    pomodoro_resume(&p, MIN(100));
    ASSERT_EQ(PM_FOCUS, p.state);
    ASSERT_EQ(MIN(15), pomodoro_remaining_ms(&p, MIN(100)));
    PASS();
}

TEST add5_extends_phase(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    pomodoro_start_focus(&p, 0);
    pomodoro_add5(&p, 0);
    ASSERT_EQ(MIN(30), pomodoro_remaining_ms(&p, 0));
    PASS();
}

TEST skip_focus_breaks_streak_and_idles(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    pomodoro_start_focus(&p, 0);
    pomodoro_tick(&p, MIN(25)); pomodoro_acknowledge(&p, MIN(25));  // streak=1
    pomodoro_tick(&p, MIN(30));                                    // idle
    pomodoro_start_focus(&p, MIN(30));
    pomodoro_skip(&p, MIN(31));
    ASSERT_EQ(PM_IDLE, p.state);
    ASSERT_EQ(0, p.stats.streak);
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(starts_idle);
    RUN_TEST(focus_runs_then_alarms);
    RUN_TEST(ack_goes_to_short_break);
    RUN_TEST(fourth_focus_earns_long_break);
    RUN_TEST(break_end_returns_to_idle);
    RUN_TEST(pause_resume_preserves_remaining);
    RUN_TEST(add5_extends_phase);
    RUN_TEST(skip_focus_breaks_streak_and_idles);
    GREATEST_MAIN_END();
}
