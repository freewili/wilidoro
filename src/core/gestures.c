#include "gestures.h"
#include <math.h>

void gesture_init(gesture_state_t *g) {
    gesture_state_t z = {0};
    *g = z;
    g->face = 0;
}

gesture_event_t gesture_feed(gesture_state_t *g, float ax, float ay, float az, uint32_t now_ms) {
    gesture_event_t out = GEV_NONE;

    /* --- jerk accumulator for shake --- */
    if (g->have_prev) {
        float dj = fabsf(ax - g->px) + fabsf(ay - g->py) + fabsf(az - g->pz);
        if (now_ms - g->jerk_ms > G_SHAKE_WIN_MS) g->jerk = 0.0f;
        g->jerk_ms = now_ms;
        g->jerk += dj;
        if (g->jerk >= G_SHAKE_JERK) { g->jerk = 0.0f; out = GEV_SHAKE; }
    }

    /* --- stillness / pickup --- */
    float mag = sqrtf(ax*ax + ay*ay + az*az);
    float dev = fabsf(mag - 1.0f);
    if (dev < G_STILL_DEV) {
        if (!g->was_still) { g->was_still = true; g->still_since_ms = now_ms; }
    } else {
        if (g->was_still && (now_ms - g->still_since_ms) >= G_STILL_MS && dev > G_PICKUP_DEV) {
            if (out == GEV_NONE) out = GEV_PICKUP;
        }
        g->was_still = false;
    }

    /* --- flip detection with hold --- */
    int face_now = g->face;
    if (az <= G_FLIP_DOWN_AZ) face_now = -1;
    else if (az >= G_FLIP_UP_AZ) face_now = 1;
    if (face_now != g->face) {
        g->face = face_now;
        g->face_since_ms = now_ms;
        g->face_reported = false;
    } else if (!g->face_reported && (now_ms - g->face_since_ms) >= G_FLIP_HOLD_MS) {
        g->face_reported = true;
        if (out == GEV_NONE) out = (g->face == -1) ? GEV_FLIP_DOWN : GEV_FLIP_UP;
    }

    g->px = ax; g->py = ay; g->pz = az; g->have_prev = true;
    return out;
}
