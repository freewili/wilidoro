// src/app/sound.c -- per-theme synthesized sound tables + a tiny time-driven
// sequencer. Pure: no LVGL, no hal, no wilibsp; host-unit-tested.
//
// Pitch floor (SOUND_HZ_MIN 440): the device HAL snaps every tone to a whole
// number of sine cycles in a 1024-frame / 16009 Hz ring buffer, a ~15.6 Hz
// grid. At 440 Hz that is <= 1.8 % (31 cents); lower notes detune audibly and
// the 0.5 W speaker cannot reproduce them anyway.
// Duration floor (SOUND_MS_MIN 40): tones are stopped from the main loop, whose
// granularity is one iteration (~5-20 ms).
// There is no intra-note envelope (the HAL's ring buffer is constant-amplitude),
// so decay is written into the table as a trailing note at the same pitch with
// a lower amp.
#include "sound.h"

/* ---- theme 0: Neon Arc -- airy rising sine figures ---- */
static const sound_note_t neon_start[]     = {{ 660, 90,150},{ 990, 90,150},{1320,140,140}};
static const sound_note_t neon_focus_end[] = {{1320,120,200},{ 990,120,200},{1320,120,200},
                                              { 990,120,200},{1320,200,190},{1320,120,110}};
static const sound_note_t neon_break_end[] = {{ 990,120,140},{ 660,180,120},{ 660,120, 70}};
static const sound_note_t neon_tick[]      = {{1760, 40, 40}};
static const sound_note_t neon_blip[]      = {{1320, 40, 55}};

/* ---- theme 1: Retro Tomato Arcade -- chiptune arpeggios (C5 E5 G5 C6 E6 G6) ---- */
static const sound_note_t arc_start[]      = {{ 523, 70,170},{ 659, 70,170},{ 784, 70,170},
                                              {1047,140,190}};
static const sound_note_t arc_focus_end[]  = {{1047,100,200},{ 784,100,200},{1047,100,200},
                                              {1319,200,215},{   0, 60,  0},{1319,100,200},
                                              {1568,220,215}};
static const sound_note_t arc_break_end[]  = {{ 784, 80,150},{1047, 80,150},{ 784,160,120}};
static const sound_note_t arc_tick[]       = {{1047, 40, 45}};
static const sound_note_t arc_blip[]       = {{ 880, 40, 60}};

/* ---- theme 2: Warm Flip Clock -- soft double chime, low-key ---- */
static const sound_note_t flip_start[]     = {{ 880,120,130},{ 660,200,110}};
static const sound_note_t flip_focus_end[] = {{ 880,180,180},{   0, 80,  0},{ 880,180,180},
                                              {   0, 80,  0},{ 660,320,170},{ 660,160, 90}};
static const sound_note_t flip_break_end[] = {{ 660,160,110},{ 880,240,100}};
static const sound_note_t flip_tick[]      = {{ 880, 40, 30}};
static const sound_note_t flip_blip[]      = {{ 660, 40, 45}};

typedef struct { const sound_note_t *seq; uint8_t n; } entry_t;
#define E(a) { (a), (uint8_t)(sizeof(a) / sizeof((a)[0])) }

/* Row = theme index (0 neon / 1 arcade / 2 flip), column = sound_id_t.
   The row order must match led_pattern.c and theme.c -- convention, not enforced. */
static const entry_t TABLE[SOUND_THEME_COUNT][SND_COUNT] = {
    { E(neon_start), E(neon_focus_end), E(neon_break_end), E(neon_tick), E(neon_blip) },
    { E(arc_start),  E(arc_focus_end),  E(arc_break_end),  E(arc_tick),  E(arc_blip)  },
    { E(flip_start), E(flip_focus_end), E(flip_break_end), E(flip_tick), E(flip_blip) },
};
#undef E

const sound_note_t *sound_seq(uint8_t theme_idx, sound_id_t id, uint8_t *n_out) {
    unsigned u = (unsigned)id;                  /* unsigned compare: no -Wtype-limits
                                                   warning about `id < 0` on an enum */
    if (u >= (unsigned)SND_COUNT) return NULL;
    if (theme_idx >= SOUND_THEME_COUNT) theme_idx = 0;
    const entry_t *e = &TABLE[theme_idx][u];
    if (n_out) *n_out = e->n;
    return e->seq;
}

void sound_reset(sound_player_t *p) {
    p->seq = 0; p->n = 0; p->i = 0; p->next_ms = 0; p->volume = 0; p->active = false;
}

void sound_play(sound_player_t *p, uint8_t theme_idx, sound_id_t id,
                uint8_t volume, uint32_t now_ms) {
    sound_reset(p);
    if (volume == 0) return;
    if (volume > 100) volume = 100;
    uint8_t n = 0;
    const sound_note_t *s = sound_seq(theme_idx, id, &n);
    if (!s || n == 0) return;
    p->seq = s; p->n = n; p->i = 0; p->next_ms = now_ms; p->volume = volume; p->active = true;
}

bool sound_active(const sound_player_t *p) { return p->active; }

bool sound_next(sound_player_t *p, uint32_t now_ms, sound_note_t *out) {
    if (!p->active) return false;
    if ((int32_t)(now_ms - p->next_ms) < 0) return false;   /* wrap-safe compare */
    if (p->i >= p->n) { p->active = false; return false; }  /* last note's tail elapsed */
    const sound_note_t *nt = &p->seq[p->i];
    out->hz  = nt->hz;
    out->ms  = nt->ms;
    out->amp = (uint8_t)((uint32_t)nt->amp * p->volume / 100u);
    p->next_ms = now_ms + nt->ms;
    p->i++;
    return true;
}
