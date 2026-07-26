// src/core/tilt.h -- pure orientation gate: is the board lying face-up and level?
#ifndef WILIDORO_TILT_H
#define WILIDORO_TILT_H
#include <stdint.h>
#include <stdbool.h>

/* Edge events. The caller acts on transitions only, never on the standing zone,
   which is what keeps a manual Pause taken while the board is flat from being
   instantly undone by the gate. */
typedef enum { TILT_EV_NONE, TILT_EV_FLAT, TILT_EV_LIFTED } tilt_event_t;

/* Thresholds on az/|a| -- the cosine of the tilt away from face-up level.
   Dividing by |a| is what rejects motion: a 2 g jolt can push raw az past
   TILT_FLAT_COS while the board is nowhere near level. */
#define TILT_FLAT_COS  (0.87f)   /* enter flat: within ~29.5 deg of level */
#define TILT_LIFT_COS  (0.77f)   /* leave flat: beyond ~39.7 deg */
#define TILT_HOLD_MS   (600u)    /* a new zone must persist this long to count */
#define TILT_MAG_MIN   (0.30f)   /* below this |a| the sample is discarded */

typedef struct {
    int8_t   zone;           /* committed: +1 flat, -1 lifted, 0 before priming */
    int8_t   cand;           /* pending zone being timed, 0 = nothing pending */
    uint32_t cand_since_ms;
    bool     primed;         /* the first usable sample adopts its zone silently */
} tilt_state_t;

void         tilt_init(tilt_state_t *t);
tilt_event_t tilt_feed(tilt_state_t *t, float ax, float ay, float az, uint32_t now_ms);
#endif
