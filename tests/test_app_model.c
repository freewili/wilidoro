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
    ASSERT(s.beacon_on);
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

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(settings_defaults_are_classic_pomodoro);
    RUN_TEST(focus_clamps_between_5_and_60_step_5);
    RUN_TEST(volume_clamps_0_100_step_10);
    RUN_TEST(theme_cycles_0_1_2);
    /* neighbor-table tests appended in Task 2 */
    GREATEST_MAIN_END();
}
