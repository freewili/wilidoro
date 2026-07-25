// src/app/timer_view.h
#ifndef WILIDORO_TIMER_VIEW_H
#define WILIDORO_TIMER_VIEW_H
#include <stdint.h>
#include <stdbool.h>
#include "pomodoro.h"

typedef struct {
    pm_state_t eff_state;    /* effective state: resume_state when paused, else state */
    bool       idle, paused, alarm, break_phase;
    uint32_t   rem_ms, total_ms;  /* idle => rem==total==focus (full ring preview) */
    unsigned   session_idx, session_n;  /* "X of N": idx = completed % long_every (guarded), n = long_every */
    unsigned   completed, streak;       /* for the arcade score line */
} timer_view_t;

timer_view_t timer_view_make(const pomodoro_t *p, bool alarm_active, uint32_t now_ms);
#endif
