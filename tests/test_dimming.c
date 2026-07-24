#include "greatest.h"
#include "dimming.h"

TEST floor_in_darkness(void) {
    dim_state_t d; dim_init(&d, 100.0f);
    uint8_t pct = 0;
    for (int i = 0; i < 50; i++) pct = dim_apply(&d, 0.0f);  /* converge */
    ASSERT_EQ(40, pct);
    PASS();
}

TEST full_in_bright_light(void) {
    dim_state_t d; dim_init(&d, 40.0f);
    uint8_t pct = 0;
    for (int i = 0; i < 50; i++) pct = dim_apply(&d, 1000.0f);
    ASSERT_EQ(100, pct);
    PASS();
}

TEST smoothing_is_gradual(void) {
    dim_state_t d; dim_init(&d, 100.0f);
    uint8_t first = dim_apply(&d, 0.0f);   /* one step toward 40 from 100 */
    ASSERT(first > 40 && first < 100);     /* not a jump */
    PASS();
}

TEST led_brightness_has_floor(void) {
    ASSERT_EQ(255, dim_led_brightness(100));
    ASSERT(dim_led_brightness(0) >= DIM_LED_FLOOR);
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(floor_in_darkness);
    RUN_TEST(full_in_bright_light);
    RUN_TEST(smoothing_is_gradual);
    RUN_TEST(led_brightness_has_floor);
    GREATEST_MAIN_END();
}
