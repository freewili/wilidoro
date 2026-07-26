#include "greatest.h"
#include "app_model.h"
#include <string.h>

TEST settings_defaults_are_classic_pomodoro(void) {
    app_settings_t s; app_settings_defaults(&s);
    ASSERT_EQ(25, s.focus_min);
    ASSERT_EQ(5,  s.short_min);
    ASSERT_EQ(15, s.long_min);
    ASSERT_EQ(4,  s.long_every);
    ASSERT_EQ(0,  s.theme);
    ASSERT_FALSE(s.beacon_on);    /* opt-in: broadcasting your name/state must not surprise you */
    ASSERT(s.dvi_on);
    ASSERT_FALSE(s.tilt_pause);   /* opt-in: setting the board down must not surprise you */
    PASS();
}

TEST focus_clamps_between_5_and_60_step_5(void) {
    app_settings_t s; app_settings_defaults(&s);
    for (int i = 0; i < 20; i++) app_settings_adjust_focus(&s, +1);
    ASSERT_EQ(60, s.focus_min);              /* clamped high */
    for (int i = 0; i < 20; i++) app_settings_adjust_focus(&s, -1);
    ASSERT_EQ(5, s.focus_min);               /* clamped low */
    app_settings_adjust_focus(&s, +1);
    ASSERT_EQ(10, s.focus_min);              /* step of 5 */
    PASS();
}

TEST volume_clamps_0_100_step_10(void) {
    app_settings_t s; app_settings_defaults(&s);
    for (int i = 0; i < 20; i++) app_settings_adjust_volume(&s, -1);
    ASSERT_EQ(0, s.volume);
    for (int i = 0; i < 20; i++) app_settings_adjust_volume(&s, +1);
    ASSERT_EQ(100, s.volume);
    PASS();
}

TEST theme_cycles_0_1_2(void) {
    app_settings_t s; app_settings_defaults(&s);
    app_settings_cycle_theme(&s); ASSERT_EQ(1, s.theme);
    app_settings_cycle_theme(&s); ASSERT_EQ(2, s.theme);
    app_settings_cycle_theme(&s); ASSERT_EQ(0, s.theme);
    PASS();
}

static beacon_msg_t mk(const char *n8, beacon_state_t st, uint8_t min, uint8_t done) {
    beacon_msg_t m; memcpy(m.name, n8, 8); m.state = st; m.minutes_left = min; m.completed = done;
    return m;
}

TEST upsert_adds_then_updates_by_name(void) {
    neighbor_table_t t; neighbor_table_init(&t);
    beacon_msg_t a = mk("JEN     ", BST_FOCUS, 12, 3);
    neighbor_upsert(&t, &a, 1000);
    ASSERT_EQ(1, neighbor_count(&t));
    beacon_msg_t a2 = mk("JEN     ", BST_BREAK, 4, 3);
    neighbor_upsert(&t, &a2, 2000);
    ASSERT_EQ(1, neighbor_count(&t));           /* same name -> update, not add */
    /* find JEN and check it updated */
    int found = -1;
    for (int i = 0; i < NEIGHBOR_MAX; i++)
        if (t.items[i].used && memcmp(t.items[i].name, "JEN     ", 8) == 0) found = i;
    ASSERT(found >= 0);
    ASSERT_EQ(BST_BREAK, t.items[found].state);
    ASSERT_EQ(4, t.items[found].minutes_left);
    ASSERT_EQ(2000, t.items[found].last_seen_ms);
    PASS();
}

TEST expire_drops_stale_keeps_fresh(void) {
    neighbor_table_t t; neighbor_table_init(&t);
    beacon_msg_t a = mk("OLD     ", BST_IDLE, 0, 1);
    beacon_msg_t b = mk("NEW     ", BST_FOCUS, 20, 2);
    neighbor_upsert(&t, &a, 1000);
    neighbor_upsert(&t, &b, 50000);
    neighbor_expire(&t, 1000 + NEIGHBOR_TTL_MS + 1);   /* OLD is stale, NEW is fresh */
    ASSERT_EQ(1, neighbor_count(&t));
    ASSERT(t.items[0].used ? memcmp(t.items[0].name,"NEW     ",8)==0 : true);
    PASS();
}

TEST upsert_evicts_oldest_when_full(void) {
    neighbor_table_t t; neighbor_table_init(&t);
    char nm[9] = "N0      ";
    for (int i = 0; i < NEIGHBOR_MAX; i++) { nm[1] = (char)('0'+i); beacon_msg_t m = mk(nm, BST_IDLE, 0, 0); neighbor_upsert(&t, &m, (uint32_t)(100+i)); }
    ASSERT_EQ(NEIGHBOR_MAX, neighbor_count(&t));
    beacon_msg_t extra = mk("EXTRA   ", BST_FOCUS, 5, 0);
    neighbor_upsert(&t, &extra, 100000);
    ASSERT_EQ(NEIGHBOR_MAX, neighbor_count(&t));       /* still full */
    /* N0 (oldest, last_seen 100) evicted; EXTRA present */
    bool has_extra = false, has_n0 = false;
    for (int i = 0; i < NEIGHBOR_MAX; i++) if (t.items[i].used) {
        if (memcmp(t.items[i].name,"EXTRA   ",8)==0) has_extra = true;
        if (memcmp(t.items[i].name,"N0      ",8)==0) has_n0 = true;
    }
    ASSERT(has_extra); ASSERT_FALSE(has_n0);
    PASS();
}

TEST tilt_gate_pauses_only_focus(void) {
    ASSERT_FALSE(tilt_gate_pauses(PM_IDLE));
    ASSERT(tilt_gate_pauses(PM_FOCUS));
    ASSERT_FALSE(tilt_gate_pauses(PM_BREAK_SHORT));
    ASSERT_FALSE(tilt_gate_pauses(PM_BREAK_LONG));
    ASSERT_FALSE(tilt_gate_pauses(PM_ALARM));
    ASSERT_FALSE(tilt_gate_pauses(PM_PAUSED));
    PASS();
}

TEST tilt_gate_resumes_only_a_focus_pause(void) {
    /* The load-bearing case: a break paused manually must never be resumed by
       setting the board down flat. */
    ASSERT(tilt_gate_resumes(PM_PAUSED, PM_FOCUS));
    ASSERT_FALSE(tilt_gate_resumes(PM_PAUSED, PM_BREAK_SHORT));
    ASSERT_FALSE(tilt_gate_resumes(PM_PAUSED, PM_BREAK_LONG));
    ASSERT_FALSE(tilt_gate_resumes(PM_PAUSED, PM_IDLE));
    ASSERT_FALSE(tilt_gate_resumes(PM_PAUSED, PM_ALARM));
    /* Not paused at all -- resume_state is irrelevant, this must stay false. */
    ASSERT_FALSE(tilt_gate_resumes(PM_IDLE, PM_FOCUS));
    ASSERT_FALSE(tilt_gate_resumes(PM_FOCUS, PM_FOCUS));
    ASSERT_FALSE(tilt_gate_resumes(PM_BREAK_SHORT, PM_FOCUS));
    ASSERT_FALSE(tilt_gate_resumes(PM_BREAK_LONG, PM_FOCUS));
    ASSERT_FALSE(tilt_gate_resumes(PM_ALARM, PM_FOCUS));
    PASS();
}

TEST beacon_msg_maps_state_and_copies_name(void) {
    app_settings_t s; app_settings_defaults(&s);
    pomodoro_t p; pomodoro_init(&p, (pm_config_t){ .focus_min = 25, .short_min = 5,
                                                   .long_min = 15, .long_every = 4 });
    beacon_msg_t m;

    /* Idle: nothing running. */
    app_beacon_msg(&s, &p, 0, &m);
    ASSERT_EQ(BST_IDLE, m.state);
    ASSERT_MEM_EQ(s.name, m.name, APP_NAME_LEN);   /* verbatim, space-padded */

    /* Focus running. */
    pomodoro_start_focus(&p, 0);
    app_beacon_msg(&s, &p, 0, &m);
    ASSERT_EQ(BST_FOCUS, m.state);
    ASSERT_EQ(25, m.minutes_left);

    /* Paused is NOT focusing -- broadcasting otherwise would freeze a
       neighbour's countdown at a stale figure. */
    pomodoro_pause(&p, 60000u);
    app_beacon_msg(&s, &p, 60000u, &m);
    ASSERT_EQ(BST_IDLE, m.state);

    /* A break reads as a break. */
    pomodoro_resume(&p, 60000u);
    pomodoro_tick(&p, 60000u + 25u * 60000u);      /* focus ends -> PM_ALARM */
    app_beacon_msg(&s, &p, 60000u + 25u * 60000u, &m);
    ASSERT_EQ(BST_IDLE, m.state);                  /* alarm is not focusing either */
    pomodoro_acknowledge(&p, 0);                   /* -> a break */
    app_beacon_msg(&s, &p, 0, &m);
    ASSERT_EQ(BST_BREAK, m.state);
    PASS();
}

TEST beacon_msg_clamps_minutes_to_a_byte(void) {
    app_settings_t s; app_settings_defaults(&s);
    /* A phase far longer than 255 minutes must not wrap the single wire byte. */
    pomodoro_t p; pomodoro_init(&p, (pm_config_t){ .focus_min = 600, .short_min = 5,
                                                   .long_min = 15, .long_every = 4 });
    pomodoro_start_focus(&p, 0);
    beacon_msg_t m;
    app_beacon_msg(&s, &p, 0, &m);
    ASSERT_EQ(255, m.minutes_left);
    PASS();
}

TEST beacon_msg_clamps_completed_to_a_byte(void) {
    app_settings_t s; app_settings_defaults(&s);
    pomodoro_t p; pomodoro_init(&p, (pm_config_t){ .focus_min = 25, .short_min = 5,
                                                   .long_min = 15, .long_every = 4 });
    p.stats.completed = 400;           /* accumulates without bound in a long session */
    beacon_msg_t m;
    app_beacon_msg(&s, &p, 0, &m);
    ASSERT_EQ(255, m.completed);
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(settings_defaults_are_classic_pomodoro);
    RUN_TEST(focus_clamps_between_5_and_60_step_5);
    RUN_TEST(volume_clamps_0_100_step_10);
    RUN_TEST(theme_cycles_0_1_2);
    RUN_TEST(upsert_adds_then_updates_by_name);
    RUN_TEST(expire_drops_stale_keeps_fresh);
    RUN_TEST(upsert_evicts_oldest_when_full);
    RUN_TEST(tilt_gate_pauses_only_focus);
    RUN_TEST(tilt_gate_resumes_only_a_focus_pause);
    RUN_TEST(beacon_msg_maps_state_and_copies_name);
    RUN_TEST(beacon_msg_clamps_minutes_to_a_byte);
    RUN_TEST(beacon_msg_clamps_completed_to_a_byte);
    GREATEST_MAIN_END();
}
