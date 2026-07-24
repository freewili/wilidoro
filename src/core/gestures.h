// src/core/gestures.h
#ifndef WILIDORO_GESTURES_H
#define WILIDORO_GESTURES_H
#include <stdint.h>
#include <stdbool.h>

typedef enum { GEV_NONE, GEV_FLIP_DOWN, GEV_FLIP_UP, GEV_SHAKE, GEV_PICKUP } gesture_event_t;

/* Tunables (units: g). az ~ +1 face-up, ~ -1 face-down. */
#define G_FLIP_DOWN_AZ  (-0.6f)
#define G_FLIP_UP_AZ    ( 0.6f)
#define G_FLIP_HOLD_MS  400u
#define G_SHAKE_JERK    (2.5f)   /* summed |delta accel| over window to trigger */
#define G_SHAKE_WIN_MS  300u
#define G_PICKUP_DEV    (0.35f)  /* |magnitude-1g| after stillness */
#define G_STILL_DEV     (0.08f)
#define G_STILL_MS      800u

typedef struct {
    int      face;            /* +1 up, -1 down, 0 unknown */
    uint32_t face_since_ms;
    bool     face_reported;   /* CRITICAL: added per task brief step 3 note */
    float    px, py, pz;      /* previous sample */
    bool     have_prev;
    float    jerk;            /* decaying jerk accumulator */
    uint32_t jerk_ms;
    uint32_t still_since_ms;  /* when device became still (or 0) */
    bool     was_still;
} gesture_state_t;

void            gesture_init(gesture_state_t *g);
gesture_event_t gesture_feed(gesture_state_t *g, float ax, float ay, float az, uint32_t now_ms);
#endif
