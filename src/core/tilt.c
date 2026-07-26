#include "tilt.h"
#include <math.h>

void tilt_init(tilt_state_t *t) {
    tilt_state_t z = {0};
    *t = z;
}

tilt_event_t tilt_feed(tilt_state_t *t, float ax, float ay, float az, uint32_t now_ms) {
    float mag = sqrtf(ax*ax + ay*ay + az*az);
    if (mag < TILT_MAG_MIN) return TILT_EV_NONE;   /* freefall, or a garbled read */
    float c = az / mag;                            /* cos of the tilt from level */

    /* Hysteresis: inside the band the committed zone stands unchanged. */
    int8_t target = t->zone;
    if      (c >= TILT_FLAT_COS) target = +1;
    else if (c <= TILT_LIFT_COS) target = -1;

    if (!t->primed) {
        /* Adopt the board's current orientation without reporting it, so
           switching the feature on mid-session cannot fire a spurious pause or
           resume. A first sample inside the dead band has nothing to adopt --
           stay unprimed and wait for one that does. */
        if (target == 0) return TILT_EV_NONE;
        t->primed = true;
        t->zone = target;
        return TILT_EV_NONE;
    }

    if (target == t->zone) { t->cand = 0; return TILT_EV_NONE; }

    if (t->cand != target) { t->cand = target; t->cand_since_ms = now_ms; return TILT_EV_NONE; }
    if ((int32_t)(now_ms - t->cand_since_ms) < (int32_t)TILT_HOLD_MS) return TILT_EV_NONE;

    t->zone = target;
    t->cand = 0;
    return (target == +1) ? TILT_EV_FLAT : TILT_EV_LIFTED;
}
