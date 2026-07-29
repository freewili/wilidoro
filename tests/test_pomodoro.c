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

/* --- pomodoro_set_config -------------------------------------------------
   Settings can change while a session is running; the "apply only when idle"
   rule lives in the UI layer (screen_settings.c), not here. This setter's job
   is narrower: update p->cfg and touch nothing else -- not state, not stats,
   not the running phase's deadline. That last part is what keeps "no
   mid-session surprise" true even after a config change lands while a phase
   is in flight (e.g. Skip landing in PM_IDLE right after adopting it). */

TEST set_config_updates_cfg_fields(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    pm_config_t nc = { .focus_min = 50, .short_min = 10, .long_min = 20, .long_every = 3 };
    pomodoro_set_config(&p, nc);
    ASSERT_EQ(50, p.cfg.focus_min);
    ASSERT_EQ(10, p.cfg.short_min);
    ASSERT_EQ(20, p.cfg.long_min);
    ASSERT_EQ(3,  p.cfg.long_every);
    PASS();
}

TEST set_config_preserves_stats_and_focus_count(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    pomodoro_start_focus(&p, 0);
    pomodoro_tick(&p, MIN(25));               // completed=1, streak=1, focus_seconds set, focus_count=1
    ASSERT_EQ(1, p.stats.completed);
    ASSERT_EQ(1, p.stats.streak);
    ASSERT_EQ(MIN(25) / 1000u, p.stats.focus_seconds);
    ASSERT_EQ(1, p.focus_count);

    pm_config_t nc = { .focus_min = 50, .short_min = 10, .long_min = 20, .long_every = 3 };
    pomodoro_set_config(&p, nc);

    ASSERT_EQ(1, p.stats.completed);
    ASSERT_EQ(1, p.stats.streak);
    ASSERT_EQ(MIN(25) / 1000u, p.stats.focus_seconds);
    ASSERT_EQ(1, p.focus_count);
    PASS();
}

TEST set_config_while_idle_does_not_start_and_next_start_uses_new_focus_min(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    pm_config_t nc = { .focus_min = 50, .short_min = 10, .long_min = 20, .long_every = 3 };
    pomodoro_set_config(&p, nc);
    ASSERT_EQ(PM_IDLE, p.state);              // did not start anything

    pomodoro_start_focus(&p, 0);
    ASSERT_EQ(PM_FOCUS, p.state);
    ASSERT_EQ(MIN(50), pomodoro_remaining_ms(&p, 0));   // new focus_min took effect
    PASS();
}

TEST set_config_mid_focus_does_not_move_running_deadline(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    pomodoro_start_focus(&p, 0);
    ASSERT_EQ(MIN(25), pomodoro_remaining_ms(&p, 0));

    pm_config_t nc = { .focus_min = 50, .short_min = 10, .long_min = 20, .long_every = 3 };
    pomodoro_set_config(&p, nc);

    ASSERT_EQ(PM_FOCUS, p.state);
    /* Deadline untouched: the phase keeps the duration it started with,
       even though p.cfg.focus_min now reads 50. */
    ASSERT_EQ(MIN(25), pomodoro_remaining_ms(&p, 0));
    ASSERT_EQ(50, p.cfg.focus_min);
    PASS();
}

/* --- 49-day rollover ---------------------------------------------------------
   hal_now_ms() is a uint32_t millisecond counter, so it wraps every ~49.7 days.
   A phase started shortly before the wrap has a phase_end_ms that wraps with it,
   landing *numerically below* now_ms. The naive `now_ms >= phase_end_ms` reads
   that as "already expired" and ends the phase on its very first tick. Every
   deadline in this project therefore uses the wrap-safe difference form,
   `(int32_t)(now - deadline) >= 0`, which stays correct across the boundary. */

#define WRAP_START ((uint32_t)(0u - MIN(1)))   /* 60 s before the counter wraps */

TEST focus_survives_the_uint32_rollover(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    pomodoro_start_focus(&p, WRAP_START);
    /* phase_end_ms has wrapped to a small value while now_ms is still huge. */
    ASSERT_EQ(MIN(25), pomodoro_remaining_ms(&p, WRAP_START));
    ASSERT_EQ(PM_EV_NONE, pomodoro_tick(&p, WRAP_START));
    ASSERT_EQ(PM_FOCUS, p.state);

    /* 90 s in, i.e. 30 s *past* the wrap: still running, with the right left. */
    uint32_t after = WRAP_START + MIN(1) + 30000u;      /* == 30000 */
    ASSERT_EQ(PM_EV_NONE, pomodoro_tick(&p, after));
    ASSERT_EQ(PM_FOCUS, p.state);
    ASSERT_EQ(MIN(25) - 90000u, pomodoro_remaining_ms(&p, after));

    /* And it still ends exactly 25 minutes after it started, not early. */
    ASSERT_EQ(PM_EV_NONE, pomodoro_tick(&p, WRAP_START + MIN(25) - 1));
    ASSERT_EQ(PM_EV_FOCUS_ENDED, pomodoro_tick(&p, WRAP_START + MIN(25)));
    ASSERT_EQ(PM_ALARM, p.state);
    PASS();
}

TEST break_survives_the_uint32_rollover(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    /* Reach a short break with its deadline straddling the wrap. */
    pomodoro_start_focus(&p, 0);
    pomodoro_tick(&p, MIN(25));
    pomodoro_acknowledge(&p, WRAP_START);
    ASSERT_EQ(PM_BREAK_SHORT, p.state);

    ASSERT_EQ(PM_EV_NONE, pomodoro_tick(&p, WRAP_START));
    ASSERT_EQ(PM_BREAK_SHORT, p.state);
    ASSERT_EQ(PM_EV_NONE, pomodoro_tick(&p, WRAP_START + MIN(5) - 1));
    ASSERT_EQ(PM_EV_BREAK_ENDED, pomodoro_tick(&p, WRAP_START + MIN(5)));
    ASSERT_EQ(PM_IDLE, p.state);
    PASS();
}

TEST resume_survives_the_uint32_rollover(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    pomodoro_start_focus(&p, 0);
    pomodoro_pause(&p, MIN(10));                 /* 15 min left */
    ASSERT_EQ(MIN(15), pomodoro_remaining_ms(&p, MIN(10)));
    /* Resuming just before the wrap re-arms a deadline that wraps with it. */
    pomodoro_resume(&p, WRAP_START);
    ASSERT_EQ(PM_FOCUS, p.state);
    ASSERT_EQ(MIN(15), pomodoro_remaining_ms(&p, WRAP_START));
    ASSERT_EQ(PM_EV_NONE, pomodoro_tick(&p, WRAP_START));
    ASSERT_EQ(PM_EV_FOCUS_ENDED, pomodoro_tick(&p, WRAP_START + MIN(15)));
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
    RUN_TEST(set_config_updates_cfg_fields);
    RUN_TEST(set_config_preserves_stats_and_focus_count);
    RUN_TEST(set_config_while_idle_does_not_start_and_next_start_uses_new_focus_min);
    RUN_TEST(set_config_mid_focus_does_not_move_running_deadline);
    RUN_TEST(focus_survives_the_uint32_rollover);
    RUN_TEST(break_survives_the_uint32_rollover);
    RUN_TEST(resume_survives_the_uint32_rollover);
    GREATEST_MAIN_END();
}
