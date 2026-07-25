#ifndef WILIDORO_SOUND_H
#define WILIDORO_SOUND_H
#include <stdint.h>
#include <stdbool.h>

/* Synthesized UI sounds, keyed by theme index (0=neon, 1=arcade, 2=flip) --
   the same convention led_pattern uses. Pure: no LVGL, no hal, no wilibsp. */

typedef enum {
    SND_START = 0,   /* focus session started */
    SND_FOCUS_END,   /* focus ended -- the alarm */
    SND_BREAK_END,   /* break ended -- gentle chime */
    SND_TICK,        /* optional quiet focus tick (once a minute) */
    SND_BLIP,        /* softkey / touch feedback */
    SND_COUNT
} sound_id_t;

#define SOUND_THEME_COUNT 3
#define SOUND_MAX_NOTES   8
#define SOUND_HZ_MIN      440u    /* below this the 15.6 Hz HAL snapping grid detunes
                                     audibly, and the 0.5 W speaker has no output anyway */
#define SOUND_HZ_MAX      3000u
#define SOUND_MS_MIN      40u     /* shorter than one main-loop stop granularity is unreliable */
#define SOUND_MS_MAX      400u
#define SOUND_SEQ_MS_MAX  2000u   /* no sequence may hog the speaker longer than this */

/* hz == 0 means a rest (silence) of `ms`. amp is 0..255 nominal loudness;
   the HAL scales it down to the speaker-safe ceiling. */
typedef struct { uint16_t hz; uint16_t ms; uint8_t amp; } sound_note_t;

typedef struct {
    const sound_note_t *seq;   /* NULL when idle */
    uint8_t  n;                /* notes in seq */
    uint8_t  i;                /* index of the next note to dispatch */
    uint32_t next_ms;          /* when note i is due */
    uint8_t  volume;           /* 0..100, captured at sound_play() */
    bool     active;
} sound_player_t;

void sound_reset(sound_player_t *p);

/* Start `id` for `theme_idx` (out-of-range clamps to 0) at `volume` 0..100,
   pre-empting anything already playing. volume == 0 queues nothing. */
void sound_play(sound_player_t *p, uint8_t theme_idx, sound_id_t id,
                uint8_t volume, uint32_t now_ms);

/* Advance the sequencer. Returns true and fills *out when a note is due now --
   the caller then hands it straight to hal_tone(out->hz, out->ms, out->amp).
   out->amp is already volume-scaled. */
bool sound_next(sound_player_t *p, uint32_t now_ms, sound_note_t *out);

/* True while a sequence is still running (including the last note's tail). */
bool sound_active(const sound_player_t *p);

/* Table accessor (used by tests and nothing else). Returns NULL for a bad id. */
const sound_note_t *sound_seq(uint8_t theme_idx, sound_id_t id, uint8_t *n_out);
#endif
