#include "greatest.h"
#include "gestures.h"

/* Feed a steady vector for a span, sampling every 50ms; return the last event seen. */
static gesture_event_t feed_steady(gesture_state_t *g, float ax, float ay, float az,
                                   uint32_t *t, uint32_t span_ms) {
    gesture_event_t last = GEV_NONE;
    for (uint32_t e = *t + span_ms; *t <= e; *t += 50) {
        gesture_event_t ev = gesture_feed(g, ax, ay, az, *t);
        if (ev != GEV_NONE) last = ev;
    }
    return last;
}

TEST flip_down_after_hold(void) {
    gesture_state_t g; gesture_init(&g);
    uint32_t t = 0;
    feed_steady(&g, 0, 0, 1.0f, &t, 500);          /* settle face-up */
    gesture_event_t ev = feed_steady(&g, 0, 0, -1.0f, &t, 600);
    ASSERT_EQ(GEV_FLIP_DOWN, ev);
    PASS();
}

TEST no_flip_if_not_held(void) {
    gesture_state_t g; gesture_init(&g);
    uint32_t t = 0;
    feed_steady(&g, 0, 0, 1.0f, &t, 500);
    /* only 100ms face-down, less than G_FLIP_HOLD_MS */
    gesture_event_t ev = feed_steady(&g, 0, 0, -1.0f, &t, 100);
    ASSERT_EQ(GEV_NONE, ev);
    PASS();
}

TEST shake_triggers(void) {
    gesture_state_t g; gesture_init(&g);
    uint32_t t = 0;
    gesture_event_t last = GEV_NONE;
    for (int i = 0; i < 8; i++) {
        float s = (i & 1) ? 2.0f : -2.0f;   /* big alternating swings */
        gesture_event_t ev = gesture_feed(&g, s, 0, 0, t); t += 30;
        if (ev == GEV_SHAKE) last = ev;
    }
    ASSERT_EQ(GEV_SHAKE, last);
    PASS();
}

TEST pickup_after_stillness(void) {
    gesture_state_t g; gesture_init(&g);
    uint32_t t = 0;
    feed_steady(&g, 0, 0, 1.0f, &t, 1000);          /* still >= G_STILL_MS */
    gesture_event_t ev = gesture_feed(&g, 1.0f, 0.0f, 1.0f, t);  /* jolt */
    ASSERT_EQ(GEV_PICKUP, ev);
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(flip_down_after_hold);
    RUN_TEST(no_flip_if_not_held);
    RUN_TEST(shake_triggers);
    RUN_TEST(pickup_after_stillness);
    GREATEST_MAIN_END();
}
