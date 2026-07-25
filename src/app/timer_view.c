#include "timer_view.h"

timer_view_t timer_view_make(const pomodoro_t *p, bool alarm_active, uint32_t now_ms) {
    timer_view_t v = {0};
    v.eff_state = (p->state == PM_PAUSED) ? p->resume_state : p->state;
    v.idle   = (p->state == PM_IDLE);
    v.paused = (p->state == PM_PAUSED);
    v.alarm  = alarm_active;
    v.break_phase = (v.eff_state == PM_BREAK_SHORT || v.eff_state == PM_BREAK_LONG);

    v.total_ms = (uint32_t)p->cfg.focus_min * 60000u;
    if (v.eff_state == PM_BREAK_SHORT) v.total_ms = (uint32_t)p->cfg.short_min * 60000u;
    else if (v.eff_state == PM_BREAK_LONG) v.total_ms = (uint32_t)p->cfg.long_min * 60000u;

    v.rem_ms = pomodoro_remaining_ms(p, now_ms);
    if (v.idle && !v.alarm) { v.total_ms = (uint32_t)p->cfg.focus_min * 60000u; v.rem_ms = v.total_ms; }

    v.session_n = p->cfg.long_every;
    v.session_idx = p->cfg.long_every ? (p->stats.completed % p->cfg.long_every) : 0;
    v.completed = p->stats.completed;
    v.streak = p->stats.streak;
    return v;
}
