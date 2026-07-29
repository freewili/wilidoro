#ifndef WILIDORO_POMODORO_H
#define WILIDORO_POMODORO_H
#include <stdint.h>
#include <stdbool.h>

typedef enum { PM_IDLE, PM_FOCUS, PM_BREAK_SHORT, PM_BREAK_LONG, PM_ALARM, PM_PAUSED } pm_state_t;
typedef enum { PM_EV_NONE, PM_EV_FOCUS_ENDED, PM_EV_BREAK_ENDED } pm_event_t;

typedef struct { uint16_t focus_min, short_min, long_min, long_every; } pm_config_t;
typedef struct { uint16_t completed; uint16_t streak; uint32_t focus_seconds; } pm_stats_t;

typedef struct {
    pm_config_t cfg;
    pm_state_t  state;
    pm_state_t  resume_state;   // state to return to from PM_PAUSED
    uint32_t    phase_end_ms;   // absolute now_ms at which the running phase ends
    uint32_t    paused_left_ms; // remaining ms captured at pause
    uint16_t    focus_count;    // completed focus sessions this cycle (for long-break cadence)
    bool        alarm_is_focus; // PM_ALARM came from focus-end (true) vs unused
    pm_stats_t  stats;
} pomodoro_t;

void       pomodoro_init(pomodoro_t *p, pm_config_t cfg);
/* Updates p->cfg only -- state, phase_end_ms, focus_count and stats are left
   exactly as they are. A running phase keeps the duration it started with
   (no mid-session surprise); the new config takes effect on the next call
   that reads it, e.g. pomodoro_start_focus() after landing in PM_IDLE. */
void       pomodoro_set_config(pomodoro_t *p, pm_config_t cfg);
void       pomodoro_start_focus(pomodoro_t *p, uint32_t now_ms);
void       pomodoro_pause(pomodoro_t *p, uint32_t now_ms);
void       pomodoro_resume(pomodoro_t *p, uint32_t now_ms);
void       pomodoro_skip(pomodoro_t *p, uint32_t now_ms);
void       pomodoro_add5(pomodoro_t *p, uint32_t now_ms);
void       pomodoro_acknowledge(pomodoro_t *p, uint32_t now_ms);
pm_event_t pomodoro_tick(pomodoro_t *p, uint32_t now_ms);
uint32_t   pomodoro_remaining_ms(const pomodoro_t *p, uint32_t now_ms);
#endif
