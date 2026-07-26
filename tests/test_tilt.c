#include "greatest.h"
#include "tilt.h"

/* Feed one steady vector for span_ms, sampling every 100 ms -- the same cadence
   sensor_cb uses on the device. Returns the last non-NONE event and counts how
   many fired, because "exactly once" is the property that matters most here. */
static tilt_event_t feed(tilt_state_t *s, float ax, float ay, float az,
                         uint32_t *t, uint32_t span_ms, int *count) {
    tilt_event_t last = TILT_EV_NONE;
    for (uint32_t end = *t + span_ms; *t <= end; *t += 100) {
        tilt_event_t ev = tilt_feed(s, ax, ay, az, *t);
        if (ev != TILT_EV_NONE) { last = ev; (*count)++; }
    }
    return last;
}

/* Vectors used throughout. UPRIGHT is the board stood on its edge: az ~ 0, so
   c ~ 0.05, comfortably past TILT_LIFT_COS. */
#define FLAT_X    0.0f
#define FLAT_Y    0.0f
#define FLAT_Z    1.0f
#define UP_X      0.0f
#define UP_Y      1.0f
#define UP_Z      0.05f

TEST primes_silently_then_lifts_once(void) {
    tilt_state_t s; tilt_init(&s);
    uint32_t t = 0; int n = 0;
    /* Priming adopts "flat" and must NOT report it. */
    ASSERT_EQ(TILT_EV_NONE, feed(&s, FLAT_X, FLAT_Y, FLAT_Z, &t, 500, &n));
    ASSERT_EQ(0, n);
    /* Stood upright: one LIFTED after the hold, and only one. */
    ASSERT_EQ(TILT_EV_LIFTED, feed(&s, UP_X, UP_Y, UP_Z, &t, 2000, &n));
    ASSERT_EQ(1, n);
    PASS();
}

TEST no_event_before_the_hold_elapses(void) {
    tilt_state_t s; tilt_init(&s);
    uint32_t t = 0; int n = 0;
    feed(&s, FLAT_X, FLAT_Y, FLAT_Z, &t, 500, &n);
    /* 400 ms lifted is short of TILT_HOLD_MS = 600. */
    ASSERT_EQ(TILT_EV_NONE, feed(&s, UP_X, UP_Y, UP_Z, &t, 400, &n));
    ASSERT_EQ(0, n);
    PASS();
}

TEST returning_to_flat_mid_hold_cancels(void) {
    tilt_state_t s; tilt_init(&s);
    uint32_t t = 0; int n = 0;
    feed(&s, FLAT_X, FLAT_Y, FLAT_Z, &t, 500, &n);
    feed(&s, UP_X, UP_Y, UP_Z, &t, 300, &n);          /* lifted, too briefly */
    feed(&s, FLAT_X, FLAT_Y, FLAT_Z, &t, 300, &n);    /* back flat: cancels */
    /* The hold must restart from scratch, so 400 ms still yields nothing. */
    ASSERT_EQ(TILT_EV_NONE, feed(&s, UP_X, UP_Y, UP_Z, &t, 400, &n));
    ASSERT_EQ(0, n);
    PASS();
}

TEST dead_band_holds_the_zone(void) {
    tilt_state_t s; tilt_init(&s);
    uint32_t t = 0; int n = 0;
    feed(&s, FLAT_X, FLAT_Y, FLAT_Z, &t, 500, &n);
    /* 35 deg: c = 0.819, inside the 0.77..0.87 band -- no zone change, ever. */
    ASSERT_EQ(TILT_EV_NONE, feed(&s, 0.0f, 0.574f, 0.819f, &t, 3000, &n));
    ASSERT_EQ(0, n);
    PASS();
}

TEST motion_is_rejected_by_normalization(void) {
    tilt_state_t s; tilt_init(&s);
    uint32_t t = 0; int n = 0;
    /* Start upright, so a spurious "flat" would be a reportable change. */
    feed(&s, UP_X, UP_Y, UP_Z, &t, 500, &n);
    /* A 2 g jolt while still upright: raw az = 1.0 clears TILT_FLAT_COS, but
       |a| = 2.24 so c = 0.45 and the board is correctly still lifted. This is
       the single most important test in the file -- it is why tilt_feed divides
       by |a| instead of thresholding az directly. */
    ASSERT_EQ(TILT_EV_NONE, feed(&s, 0.0f, 2.0f, 1.0f, &t, 2000, &n));
    ASSERT_EQ(0, n);
    PASS();
}

TEST freefall_samples_are_discarded(void) {
    tilt_state_t s; tilt_init(&s);
    uint32_t t = 0; int n = 0;
    feed(&s, FLAT_X, FLAT_Y, FLAT_Z, &t, 500, &n);       /* primed flat */
    /* |a| = 0.05, below TILT_MAG_MIN: ignored, zone untouched. */
    ASSERT_EQ(TILT_EV_NONE, feed(&s, 0.0f, 0.0f, 0.05f, &t, 2000, &n));
    ASSERT_EQ(0, n);
    /* State is intact: a genuine lift still reports normally afterwards. */
    ASSERT_EQ(TILT_EV_LIFTED, feed(&s, UP_X, UP_Y, UP_Z, &t, 1000, &n));
    ASSERT_EQ(1, n);
    PASS();
}

TEST flat_again_reports_once(void) {
    tilt_state_t s; tilt_init(&s);
    uint32_t t = 0; int n = 0;
    feed(&s, FLAT_X, FLAT_Y, FLAT_Z, &t, 500, &n);
    ASSERT_EQ(TILT_EV_LIFTED, feed(&s, UP_X, UP_Y, UP_Z, &t, 1000, &n));
    n = 0;
    ASSERT_EQ(TILT_EV_FLAT, feed(&s, FLAT_X, FLAT_Y, FLAT_Z, &t, 2000, &n));
    ASSERT_EQ(1, n);
    PASS();
}

TEST first_sample_inside_the_band_defers_priming(void) {
    tilt_state_t s; tilt_init(&s);
    uint32_t t = 0; int n = 0;
    /* 35 deg from the very first sample: there is no zone to adopt yet. */
    feed(&s, 0.0f, 0.574f, 0.819f, &t, 500, &n);
    ASSERT_EQ(0, n);
    /* Settling flat primes silently -- it must not look like a transition. */
    ASSERT_EQ(TILT_EV_NONE, feed(&s, FLAT_X, FLAT_Y, FLAT_Z, &t, 2000, &n));
    ASSERT_EQ(0, n);
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(primes_silently_then_lifts_once);
    RUN_TEST(no_event_before_the_hold_elapses);
    RUN_TEST(returning_to_flat_mid_hold_cancels);
    RUN_TEST(dead_band_holds_the_zone);
    RUN_TEST(motion_is_rejected_by_normalization);
    RUN_TEST(freefall_samples_are_discarded);
    RUN_TEST(flat_again_reports_once);
    RUN_TEST(first_sample_inside_the_band_defers_priming);
    GREATEST_MAIN_END();
}
