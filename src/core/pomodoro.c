#include "pomodoro.h"

void pomodoro_init(pomodoro_t *p, pm_config_t cfg) {
    pomodoro_t z = {0};
    *p = z;
    p->cfg = cfg;
    p->state = PM_IDLE;
}

void pomodoro_start_focus(pomodoro_t *p, uint32_t now_ms) {
    p->state = PM_FOCUS;
    p->phase_end_ms = now_ms + (uint32_t)p->cfg.focus_min * 60000u;
}

void pomodoro_pause(pomodoro_t *p, uint32_t now_ms) {
    if (p->state != PM_FOCUS && p->state != PM_BREAK_SHORT && p->state != PM_BREAK_LONG) return;
    p->paused_left_ms = pomodoro_remaining_ms(p, now_ms);
    p->resume_state = p->state;
    p->state = PM_PAUSED;
}

void pomodoro_resume(pomodoro_t *p, uint32_t now_ms) {
    if (p->state != PM_PAUSED) return;
    p->state = p->resume_state;
    p->phase_end_ms = now_ms + p->paused_left_ms;
}

void pomodoro_skip(pomodoro_t *p, uint32_t now_ms) {
    (void)now_ms;
    if (p->state == PM_FOCUS || p->state == PM_ALARM) p->stats.streak = 0;
    p->state = PM_IDLE;
}

void pomodoro_add5(pomodoro_t *p, uint32_t now_ms) {
    (void)now_ms;
    if (p->state == PM_FOCUS || p->state == PM_BREAK_SHORT || p->state == PM_BREAK_LONG)
        p->phase_end_ms += 5u * 60000u;
}

void pomodoro_acknowledge(pomodoro_t *p, uint32_t now_ms) {
    if (p->state != PM_ALARM) return;
    if (p->cfg.long_every != 0 && (p->focus_count % p->cfg.long_every) == 0) {
        p->state = PM_BREAK_LONG;
        p->phase_end_ms = now_ms + (uint32_t)p->cfg.long_min * 60000u;
    } else {
        p->state = PM_BREAK_SHORT;
        p->phase_end_ms = now_ms + (uint32_t)p->cfg.short_min * 60000u;
    }
}

pm_event_t pomodoro_tick(pomodoro_t *p, uint32_t now_ms) {
    if (p->state == PM_FOCUS && now_ms >= p->phase_end_ms) {
        p->stats.completed++;
        p->stats.streak++;
        p->stats.focus_seconds += (uint32_t)p->cfg.focus_min * 60u;
        p->focus_count++;
        p->alarm_is_focus = true;
        p->state = PM_ALARM;
        return PM_EV_FOCUS_ENDED;
    }
    if ((p->state == PM_BREAK_SHORT || p->state == PM_BREAK_LONG) && now_ms >= p->phase_end_ms) {
        p->state = PM_IDLE;
        return PM_EV_BREAK_ENDED;
    }
    return PM_EV_NONE;
}

uint32_t pomodoro_remaining_ms(const pomodoro_t *p, uint32_t now_ms) {
    if (p->state == PM_PAUSED) return p->paused_left_ms;
    if (p->state == PM_FOCUS || p->state == PM_BREAK_SHORT || p->state == PM_BREAK_LONG) {
        return p->phase_end_ms > now_ms ? p->phase_end_ms - now_ms : 0;
    }
    return 0;
}
