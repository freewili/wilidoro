# Wilidoro Plan C2 — Audio (NAU88C10 tones + per-theme sound set) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `hal_tone`/`hal_audio_idle` real on the FreeWili 2 via the wilibsp NAU88C10 + I2S stack, and give the app a per-theme synthesized sound set (start chime, focus-end alarm, break-end chime, optional focus tick, softkey blips) scaled by the existing `app_settings_t.volume`.

**Architecture:** A new pure, host-tested `src/app/sound` module owns the *what*: a static per-theme table of note sequences (`hz`, `ms`, `amp`) plus a tiny time-driven sequencer that hands one note at a time to `hal_tone()`. The device HAL owns the *how*: a single 1024-frame ring buffer filled by `tone_gen_fill`, played with the zero-CPU `audio_i2s_duplex_play_loop` DMA ring, stopped on a deadline checked in `hal_pump()`, with the codec output stage powered down when idle. The sim HAL stays a no-op. Nothing in `app/`, `ui/`, or `core/` touches wilibsp.

**Tech Stack:** C11, wilibsp (`codec_nau88c10`, `audio_i2s_duplex`, `tone_gen`), LVGL 9 timers, SDL sim, CTest + `tests/greatest.h`.

## Global Constraints

- **This is Plan C2 of the Plan C hardware layer.** C1 (ambient) is done and merged. C3 (IMU gestures) and C4 (CC1101 beacon) are separate plans — do not start them here.
- **`sound` is PURE and LVGL-free / hardware-free.** It includes only `<stdint.h>`/`<stdbool.h>` (no `hal.h`, no `lvgl.h`, no wilibsp) so it is host-unit-tested. It emits notes; it never calls `hal_tone` itself.
- **HAL is the only hardware seam.** `src/hal/hal.h` is unchanged by this plan — `hal_tone(uint16_t hz, uint16_t ms, uint8_t amp)` and `hal_audio_idle(void)` already exist as stubs. Only `hal_target.c` gains real wilibsp calls.
- **`hal_tone` MUST be non-blocking.** The main loop is `hal_pump(); lv_timer_handler(); sleep_ms(5);` — never `sleep_ms(ms)` inside `hal_tone`. Duration is enforced by a deadline checked in `hal_pump()`.
- **Do NOT modify the `wilibsp/` submodule.** It is a pinned upstream git submodule; any change to it is not captured by this repo. Everything C2 needs is already in `bsp/audio/*` and already compiled into the `freewili2_bsp` static library (`wilibsp/bsp/CMakeLists.txt` lines 16–19) and exported via `fw2.h`.
- **Do NOT call `audio_capture_start()`.** C2 is playback only. Skipping capture means C2 registers **no** `DMA_IRQ_0` handler at all (`audio_i2s_duplex_play_loop` is a pure chained DMA ring with no IRQ), so wilibsp invariant 4 (DMA_IRQ_0 is shared, never exclusive) is satisfied by construction.
- **PIO:** I2S is `pio0` (fixed in the BSP driver); wilidoro's WS2812 LEDs are on `pio1` (`hal_target.c:43`). No conflict — do not move either.
- **Sample rate is 16 kHz nominal / 16009 Hz actual.** MCLK is an integer PWM divide of the 250 MHz `clk_sys` (`250e6/61 = 4.0984 MHz`), so the codec's real fs is `4.0984e6/256 = 16009 Hz`. Use `16000` as the `audio_i2s_duplex_init()` argument (it derives the divider) but use **16009** for all frequency math. Do not "fix" the BSP's clkdiv derivation — see `wilibsp/docs/hardware/facts.md` "Audio: lock LRCK to MCLK/256".
- **0.5 W speaker limit** (wilibsp AGENTS.md invariant 10). Enforced in `hal_target.c` by scaling every tone's digital amplitude by `TONE_AMP_CAP / 255`, and by powering the output stage down when idle (`codec_nau88c10_speaker_low_power()`).
- **Sim audio is a no-op.** `src/hal/hal_sim.c` `hal_tone`/`hal_audio_idle` stay exactly as they are. Do not add SDL audio.
- **Hardware access rule:** builds + host tests + sim launch run freely. Flashing / RTT / debug probe / eMeet camera require asking the user first. All on-device listening checks are **user-gated** and deferred to a separate session.
- **Commit trailer on every commit:** `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`

## Key design decision — the one-shot tone model

`audio_i2s_duplex_play_loop(buf, frames)` loops **forever** over a DMA read-ring; there is no "play once for N ms" primitive. The model this plan uses:

1. A single static buffer `s_tone[1024]` (1024 frames × 4 bytes = **4096 bytes**, `__attribute__((aligned(4096)))`) satisfies the driver's power-of-two-bytes + aligned-to-its-size ring requirement.
2. `hal_tone()` snaps the requested frequency to the nearest one that fits a **whole number of sine cycles** in 1024 frames: `cycles = round(hz * 1024 / 16009)`, `actual_hz = cycles * 16009 / 1024`. Whole cycles ⇒ the ring wraps phase-continuously ⇒ **no loop-seam click**, ever.
3. The snapping grid is `16009/1024 ≈ 15.6 Hz`. That is why the sound tables live at **≥ 440 Hz** (≤ 1.8 % / 31 cents error, and falling with pitch) — which also suits a tiny 0.5 W speaker that cannot reproduce bass anyway. This is a deliberate constraint, not an accident.
4. `hal_tone()` stores `s_tone_end = now + ms` and returns immediately. `hal_pump()` (already called every ~5 ms in `src/target/main.c:22`) calls `audio_i2s_duplex_play_stop()` when the deadline passes. Duration granularity is therefore one main-loop iteration (~5–20 ms, longer during a big LVGL flush) — which is why **no note in the sound tables is shorter than 40 ms**.
5. Multi-note sequences are built in the app layer, not the HAL: the `sound` sequencer calls `hal_tone()` once per note, driven by a **20 ms** LVGL timer (the existing app tick is 200 ms — far too coarse for 60–150 ms notes).
6. There is no intra-note amplitude envelope (a looping ring buffer has constant amplitude by construction). Decay is expressed *in the table* as a trailing note at the same pitch with a lower `amp`.

---

## File Structure

```
src/app/
  sound.h/.c        NEW  PURE host-tested: per-theme note tables + time-driven sequencer
  app.h             MOD  app_t gains sound_player_t; app_sound()/app_sound_stop() declared
  app.c             MOD  20 ms sound timer; pomodoro events -> sounds; focus tick; alarm re-ring;
                         blips for physical softkeys
src/ui/
  ui.c              MOD  softkey CLICKED handler emits a blip (covers touch on all three screens)
  screen_timer.c    MOD  Start -> start chime; alarm Dismiss -> app_sound_stop()
src/hal/
  hal_target.c      MOD  codec + I2S bring-up in hal_init; real hal_tone/hal_audio_idle;
                         deadline + idle power-down in hal_pump; caps.audio
  hal_sim.c         ---  UNCHANGED (no-op audio, by design)
  hal.h             ---  UNCHANGED (hal_tone/hal_audio_idle already declared)
tests/
  test_sound.c      NEW  table integrity, sequencing/timing, volume scaling, pre-emption, rests
  CMakeLists.txt    MOD  add test_sound (compiles sound.c only)
CMakeLists.txt              MOD  add src/app/sound.c to the wilidoro target
src/sim/CMakeLists.txt      MOD  add ../app/sound.c to the wilidoro_sim target
docs/hardware-notes.md      MOD  audio section: amplitude cap, 0.5 W reasoning, frequency grid
```

## Verification model

| What | How | Autonomous? |
|---|---|---|
| `sound` tables + sequencer | host CTest (`tools/test.ps1`), suite goes 8 → 9 binaries | yes |
| Device HAL audio | `tools/build.ps1 -Clean` links clean, no warnings, no SRAM overflow | yes |
| App/UI wiring | host suite + device build + sim build + sim launch smoke | yes |
| **Does it actually make sound / sound good** | flash + listen on real hardware | **NO — user-gated**, deferred to a listening session (Task 4 writes the checklist) |

Report this honestly at the end: C2 is code-complete and build-verified, **not** listened-to.

---

### Task 1: `sound` — per-theme note tables + sequencer (pure, TDD)

**Files:**
- Create: `src/app/sound.h`, `src/app/sound.c`, `tests/test_sound.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing (standalone; `<stdint.h>`, `<stdbool.h>` only).
- Produces (this exact header — later tasks depend on these names verbatim):

```c
// src/app/sound.h
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
```

- [ ] **Step 1: Write the failing test**

Create `tests/test_sound.c`:

```c
#include "greatest.h"
#include "sound.h"

/* --- table integrity: every theme x every sound obeys the documented bounds --- */
TEST tables_are_well_formed(void) {
    for (uint8_t t = 0; t < SOUND_THEME_COUNT; t++) {
        for (int id = 0; id < SND_COUNT; id++) {
            uint8_t n = 0;
            const sound_note_t *s = sound_seq(t, (sound_id_t)id, &n);
            ASSERT(s != NULL);
            ASSERT(n >= 1 && n <= SOUND_MAX_NOTES);
            uint32_t total = 0;
            for (uint8_t k = 0; k < n; k++) {
                if (s[k].hz != 0) {
                    ASSERT(s[k].hz >= SOUND_HZ_MIN);
                    ASSERT(s[k].hz <= SOUND_HZ_MAX);
                }
                ASSERT(s[k].ms >= SOUND_MS_MIN);
                ASSERT(s[k].ms <= SOUND_MS_MAX);
                total += s[k].ms;
            }
            ASSERT(total <= SOUND_SEQ_MS_MAX);
        }
    }
    PASS();
}

TEST bad_id_returns_null(void) {
    uint8_t n = 99;
    ASSERT(sound_seq(0, SND_COUNT, &n) == NULL);
    PASS();
}

TEST out_of_range_theme_clamps_to_zero(void) {
    uint8_t n0 = 0, n7 = 0;
    const sound_note_t *a = sound_seq(0, SND_START, &n0);
    const sound_note_t *b = sound_seq(7, SND_START, &n7);
    ASSERT_EQ(a, b);
    ASSERT_EQ(n0, n7);
    PASS();
}

/* --- sequencing --- */
TEST first_note_dispatches_immediately(void) {
    sound_player_t p; sound_reset(&p);
    uint8_t n = 0; const sound_note_t *s = sound_seq(1, SND_START, &n);
    sound_play(&p, 1, SND_START, 100, 1000);
    sound_note_t out;
    ASSERT(sound_next(&p, 1000, &out));
    ASSERT_EQ(s[0].hz, out.hz);
    ASSERT_EQ(s[0].ms, out.ms);
    ASSERT_EQ(s[0].amp, out.amp);          /* volume 100 => unscaled */
    PASS();
}

TEST second_note_waits_for_the_first_to_finish(void) {
    sound_player_t p; sound_reset(&p);
    uint8_t n = 0; const sound_note_t *s = sound_seq(1, SND_START, &n);
    ASSERT(n >= 2);
    sound_play(&p, 1, SND_START, 100, 1000);
    sound_note_t out;
    ASSERT(sound_next(&p, 1000, &out));
    ASSERT_FALSE(sound_next(&p, 1000 + s[0].ms - 1, &out));
    ASSERT(sound_next(&p, 1000 + s[0].ms, &out));
    ASSERT_EQ(s[1].hz, out.hz);
    PASS();
}

TEST sequence_ends_after_the_last_note_tail(void) {
    sound_player_t p; sound_reset(&p);
    uint8_t n = 0; const sound_note_t *s = sound_seq(2, SND_BREAK_END, &n);
    sound_play(&p, 2, SND_BREAK_END, 100, 0);
    uint32_t t = 0; sound_note_t out;
    for (uint8_t k = 0; k < n; k++) {
        ASSERT(sound_next(&p, t, &out));
        t += s[k].ms;
    }
    ASSERT(sound_active(&p));              /* last note still sounding */
    ASSERT_FALSE(sound_next(&p, t, &out)); /* tail elapsed -> nothing more */
    ASSERT_FALSE(sound_active(&p));
    PASS();
}

/* --- volume --- */
TEST volume_scales_amplitude(void) {
    sound_player_t p; sound_reset(&p);
    uint8_t n = 0; const sound_note_t *s = sound_seq(0, SND_FOCUS_END, &n);
    sound_play(&p, 0, SND_FOCUS_END, 50, 0);
    sound_note_t out;
    ASSERT(sound_next(&p, 0, &out));
    ASSERT_EQ((uint8_t)((uint32_t)s[0].amp * 50u / 100u), out.amp);
    ASSERT_EQ(s[0].hz, out.hz);            /* pitch is unaffected by volume */
    PASS();
}

TEST volume_zero_plays_nothing(void) {
    sound_player_t p; sound_reset(&p);
    sound_play(&p, 0, SND_FOCUS_END, 0, 0);
    sound_note_t out;
    ASSERT_FALSE(sound_active(&p));
    ASSERT_FALSE(sound_next(&p, 0, &out));
    PASS();
}

/* --- pre-emption --- */
TEST a_new_sound_preempts_the_current_one(void) {
    sound_player_t p; sound_reset(&p);
    sound_note_t out;
    sound_play(&p, 1, SND_BLIP, 100, 0);
    ASSERT(sound_next(&p, 0, &out));
    uint8_t n = 0; const sound_note_t *alarm = sound_seq(1, SND_FOCUS_END, &n);
    sound_play(&p, 1, SND_FOCUS_END, 100, 10);
    ASSERT(sound_next(&p, 10, &out));      /* restarts at the alarm's note 0 */
    ASSERT_EQ(alarm[0].hz, out.hz);
    PASS();
}

/* --- rests --- */
TEST a_rest_is_dispatched_as_silence(void) {
    sound_player_t p; sound_reset(&p);
    uint8_t n = 0; const sound_note_t *s = sound_seq(2, SND_FOCUS_END, &n);
    int rests = 0;
    for (uint8_t k = 0; k < n; k++) if (s[k].hz == 0) rests++;
    ASSERT(rests > 0);                     /* the flip alarm is a double chime */
    sound_play(&p, 2, SND_FOCUS_END, 100, 0);
    uint32_t t = 0; sound_note_t out; int seen = 0;
    for (uint8_t k = 0; k < n; k++) {
        ASSERT(sound_next(&p, t, &out));
        if (out.hz == 0) { ASSERT_EQ(0, out.amp); seen++; }
        t += s[k].ms;
    }
    ASSERT_EQ(rests, seen);
    PASS();
}

TEST idle_player_yields_nothing(void) {
    sound_player_t p; sound_reset(&p);
    sound_note_t out;
    ASSERT_FALSE(sound_active(&p));
    ASSERT_FALSE(sound_next(&p, 12345, &out));
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(tables_are_well_formed);
    RUN_TEST(bad_id_returns_null);
    RUN_TEST(out_of_range_theme_clamps_to_zero);
    RUN_TEST(first_note_dispatches_immediately);
    RUN_TEST(second_note_waits_for_the_first_to_finish);
    RUN_TEST(sequence_ends_after_the_last_note_tail);
    RUN_TEST(volume_scales_amplitude);
    RUN_TEST(volume_zero_plays_nothing);
    RUN_TEST(a_new_sound_preempts_the_current_one);
    RUN_TEST(a_rest_is_dispatched_as_silence);
    RUN_TEST(idle_player_yields_nothing);
    GREATEST_MAIN_END();
}
```

Add to `tests/CMakeLists.txt` (append at the end, matching the existing `test_app_model` block style):

```cmake
add_executable(test_sound test_sound.c ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/sound.c)
target_include_directories(test_sound PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../src/app)
target_compile_options(test_sound PRIVATE -Wall -Wextra)
target_link_libraries(test_sound m)
add_test(NAME test_sound COMMAND test_sound)
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `powershell -File tools/test.ps1`
Expected: the CMake configure or the build FAILS — `src/app/sound.c` does not exist yet.

- [ ] **Step 3: Write `src/app/sound.h`**

Create it verbatim from the **Interfaces → Produces** block above.

- [ ] **Step 4: Write `src/app/sound.c`**

```c
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
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `powershell -File tools/test.ps1`
Expected: **9/9 tests pass** (the 8 existing binaries plus `test_sound`).

- [ ] **Step 6: Commit**

```bash
git add src/app/sound.h src/app/sound.c tests/test_sound.c tests/CMakeLists.txt
git commit -m "feat(app): add pure per-theme sound tables + note sequencer

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 2: Device HAL — real `hal_tone` / `hal_audio_idle` over the NAU88C10

**Files:**
- Modify: `src/hal/hal_target.c` (includes, new statics, `hal_init`, `hal_pump`, `hal_tone`, `hal_audio_idle`, `hal_caps`)

**Interfaces:**
- Consumes: wilibsp via `fw2.h` — `codec_nau88c10_init(void)`, `codec_nau88c10_input_ok(void) -> bool`, `codec_nau88c10_dac_mute(bool)`, `codec_nau88c10_set_output(codec_out_t)`, `codec_nau88c10_speaker_low_power(void)`, `audio_i2s_duplex_init(uint32_t)`, `audio_i2s_duplex_play_loop(const uint32_t *, uint)`, `audio_i2s_duplex_play_stop(void)`, `tone_gen_fill(int16_t *, unsigned, float, float, float *)`.
- Produces: no new public symbols — `hal_tone`/`hal_audio_idle` (declared in `src/hal/hal.h`) become real, and `hal_caps().audio` becomes true when the codec answers. `hal.h` is NOT edited.

**Context the implementer needs:**
- `hal_init()` runs after `board_init()` (see `src/target/main.c:11,17`), so the 250 MHz clock, I2C1 and the ioexp are already up.
- `hal_pump()` is called every main-loop iteration (`src/target/main.c:22`), roughly every 5–20 ms.
- `audio_i2s_duplex_play_loop` claims two DMA channels on first use and registers **no** IRQ handler. Do not add one. Do not call `audio_capture_start()`.
- `hal_now_ms()` already exists in this file.

- [ ] **Step 1: Add the audio includes and state**

In `src/hal/hal_target.c`, `fw2.h` is already included (line 2) and pulls in `audio/tone_gen.h`, `audio/codec_nau88c10.h`, `audio/audio_i2s_duplex.h`. Add `#include "platform/diag.h"` next to the existing includes.

Add this block just above `static bool s_light;`:

```c
/* ---------------- audio ----------------------------------------------------
 * One-shot tones over the BSP's looping DMA ring:
 *  - TONE_FRAMES frames * 4 bytes = 4096 B: power-of-two AND aligned to its own
 *    size, as audio_i2s_duplex_play_loop's read-ring requires.
 *  - The requested pitch is snapped to a whole number of sine cycles inside the
 *    buffer, so the ring wraps phase-continuously (no seam click). The grid is
 *    AUDIO_FS_HZ/TONE_FRAMES ~ 15.6 Hz; src/app/sound.c keeps every note >= 440 Hz
 *    so the resulting detune stays under ~2 %.
 *  - Duration is a deadline serviced by hal_pump(), never a busy-wait: hal_tone
 *    must not block the LVGL main loop.
 *  - AUDIO_FS_HZ is the REAL rate: MCLK is an integer PWM divide of the 250 MHz
 *    clk_sys (250e6/61 = 4.0984 MHz), so fs = 4.0984e6/256 = 16009 Hz, not 16000.
 *    audio_i2s_duplex_init() still takes the nominal 16000 (it derives the divider
 *    from it). See wilibsp/docs/hardware/facts.md, "Audio: lock LRCK to MCLK/256".
 *  - The speaker is 0.5 W (wilibsp AGENTS.md invariant 10). Every tone's amplitude
 *    is SCALED (not clamped, so the table keeps its dynamics) by TONE_AMP_CAP/255,
 *    and the output stage is powered down after AUDIO_IDLE_MS of silence.
 */
#define TONE_FRAMES    1024u
#define AUDIO_FS_HZ    16009u
#define TONE_AMP_CAP   160u      /* of 255; tone_gen peaks at 28000/32768, so the
                                    ceiling is ~0.54 full scale. Comfortable-volume
                                    default, same spirit as LED_BRIGHT_MAX. */
#define AUDIO_IDLE_MS  1500u     /* silence before the speaker stage powers down */

static uint32_t s_tone[TONE_FRAMES] __attribute__((aligned(4096)));
static bool     s_audio_ok;      /* codec answered at boot */
static bool     s_audio_awake;   /* output stage powered up */
static bool     s_tone_pending;  /* a tone/rest deadline is running */
static bool     s_idle_pending;  /* an idle power-down deadline is running */
static uint32_t s_tone_end_ms, s_idle_at_ms;

static void audio_wake(void) {
    if (s_audio_awake) return;
    codec_nau88c10_dac_mute(false);            /* clear the boot/idle soft-mute */
    codec_nau88c10_set_output(CODEC_OUT_SPEAKER);
    s_audio_awake = true;
}

static void audio_sleep(void) {
    if (!s_audio_awake) return;
    codec_nau88c10_speaker_low_power();         /* soft-mute + spk mute + 5V boost off */
    s_audio_awake = false;
}

/* Fill the ring with `cycles` whole periods and start the loop. */
static void tone_arm(uint16_t hz, uint8_t amp) {
    uint32_t cycles = ((uint32_t)hz * TONE_FRAMES + AUDIO_FS_HZ / 2u) / AUDIO_FS_HZ;
    if (cycles < 1u) cycles = 1u;
    if (cycles > TONE_FRAMES / 4u) cycles = TONE_FRAMES / 4u;   /* >=4 samples/period */
    float actual_hz = (float)cycles * (float)AUDIO_FS_HZ / (float)TONE_FRAMES;
    uint32_t eff = (uint32_t)amp * TONE_AMP_CAP / 255u;         /* 0.5 W ceiling */

    static int16_t mono[TONE_FRAMES];
    float phase = 0.0f;
    tone_gen_fill(mono, TONE_FRAMES, actual_hz, (float)AUDIO_FS_HZ, &phase);
    for (unsigned i = 0; i < TONE_FRAMES; i++) {
        uint16_t s = (uint16_t)(int16_t)(((int32_t)mono[i] * (int32_t)eff) / 255);
        s_tone[i] = ((uint32_t)s << 16) | s;    /* same sample on both I2S slots */
    }
    audio_wake();
    audio_i2s_duplex_play_loop(s_tone, TONE_FRAMES);
}

/* Deadline service, called from hal_pump() every main-loop iteration. */
static void audio_pump(uint32_t now) {
    if (s_tone_pending && (int32_t)(now - s_tone_end_ms) >= 0) {
        audio_i2s_duplex_play_stop();
        s_tone_pending = false;
        s_idle_pending = true;
        s_idle_at_ms   = now + AUDIO_IDLE_MS;
    }
    if (s_idle_pending && (int32_t)(now - s_idle_at_ms) >= 0) {
        audio_sleep();
        s_idle_pending = false;
    }
}
```

- [ ] **Step 2: Bring the codec up in `hal_init` and service it in `hal_pump`**

Replace the body of `hal_init` / `hal_pump` in `src/hal/hal_target.c` with:

```c
void hal_init(void) {
    uartkbd_init();
    ws2812_init(pio1, (uint)pio_claim_unused_sm(pio1, true), PIN_LED_DATA);
    ws2812_set_brightness(40);
    ws2812_clear();
    ws2812_show();
    bl_pwm_init();       /* backlight full-on; auto-dim is Plan C1 */
    s_light = opt4001_init();

    /* Audio: codec regs over I2C1, then MCLK + PIO0 I2S. Playback only -- we do
       NOT call audio_capture_start(), so wilidoro registers no DMA_IRQ_0 handler.
       Park the speaker stage powered-down; the first hal_tone() wakes it. */
    codec_nau88c10_init();
    s_audio_ok = codec_nau88c10_input_ok();     /* reg 0x3F rev != 0 => part is alive */
    DIAG("audio: codec %s\n", s_audio_ok ? "ok" : "ABSENT (tones disabled)");
    audio_i2s_duplex_init(16000);               /* nominal; real fs is AUDIO_FS_HZ */
    codec_nau88c10_speaker_low_power();          /* park the output stage down */
    s_audio_awake = false;
}

void hal_pump(void) { uartkbd_task(); audio_pump(hal_now_ms()); }
```

`codec_nau88c10_init()` leaves the DAC soft-muted, so the boot `speaker_low_power()`
is belt-and-braces: it also mutes the speaker output and drops the 5 V boost, which
is the state `audio_sleep()` maintains between tones. Calling `audio_sleep()` here
instead would be a no-op (`s_audio_awake` is already false), hence the direct call.

- [ ] **Step 3: Implement `hal_tone` / `hal_audio_idle` and report the capability**

Replace the two stub lines (`src/hal/hal_target.c:69-70`) with:

```c
void hal_tone(uint16_t hz, uint16_t ms, uint8_t amp) {
    if (!s_audio_ok || ms == 0) return;
    audio_i2s_duplex_play_stop();       /* always stop before re-arming the ring */
    if (hz != 0 && amp != 0) tone_arm(hz, amp);
    /* hz == 0 (a rest) leaves the DAC parked at silence for `ms`. */
    s_tone_end_ms  = hal_now_ms() + ms;
    s_tone_pending = true;
    s_idle_pending = false;
}

void hal_audio_idle(void) {
    audio_i2s_duplex_play_stop();
    s_tone_pending = false;
    s_idle_pending = false;
    audio_sleep();
}
```

And update `hal_caps` so the timer face can cross audio out when the codec is missing:

```c
hal_caps_t hal_caps(void) {
    hal_caps_t c = { .radio=false,.imu=false,.light=s_light,.audio=s_audio_ok,.buttons=true,.leds=true };
    return c;
}
```

- [ ] **Step 4: Build the device firmware**

Run: `powershell -File tools/build.ps1 -Clean`
Expected: PASS — `build/wilidoro.uf2` produced, **zero warnings**, no `region RAM overflowed` (the new statics add 4 KB `s_tone` + 2 KB `mono` = 6 KB of BSS).

If the compiler warns about `int16_t mono[TONE_FRAMES]` being large on the stack — it is `static`, so it is not; if you see the warning, you dropped the `static`.

- [ ] **Step 5: Run the host suite (regression gate)**

Run: `powershell -File tools/test.ps1`
Expected: 9/9 pass (`hal_target.c` is not host-compiled, so this is purely a no-regression check).

- [ ] **Step 6: Commit**

```bash
git add src/hal/hal_target.c
git commit -m "feat(hal): real one-shot tones on the NAU88C10 via the I2S DMA ring

hal_tone snaps the pitch to whole cycles in a 1024-frame aligned ring buffer
(seamless loop), arms audio_i2s_duplex_play_loop, and stops on a deadline
serviced by hal_pump -- never blocking the LVGL loop. Amplitude is scaled by
TONE_AMP_CAP/255 and the speaker stage powers down after 1.5 s of silence,
respecting the 0.5 W speaker. Playback only: no audio_capture, so no
DMA_IRQ_0 handler is registered.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 3: Wire the sounds into the app and the UI

**Files:**
- Modify: `src/app/app.h`, `src/app/app.c`, `src/ui/ui.c`, `src/ui/screen_timer.c`, `CMakeLists.txt`, `src/sim/CMakeLists.txt`

**Interfaces:**
- Consumes: `sound.h` from Task 1 (`sound_player_t`, `sound_play`, `sound_next`, `sound_active`, `sound_reset`, `sound_id_t`), `hal.h` (`hal_tone`, `hal_audio_idle`).
- Produces: `void app_sound(sound_id_t id);` and `void app_sound_stop(void);` in `src/app/app.h`, callable from `src/ui/*`.

- [ ] **Step 1: Add `sound.c` to both build targets**

In `CMakeLists.txt`, in the `add_executable(wilidoro ...)` source list, add after `src/app/led_pattern.c`:

```cmake
    src/app/sound.c
```

In `src/sim/CMakeLists.txt`, in the `add_executable(wilidoro_sim ...)` source list, add after the `led_pattern.c` line:

```cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/../app/sound.c
```

- [ ] **Step 2: Extend `src/app/app.h`**

```c
#ifndef WILIDORO_APP_H
#define WILIDORO_APP_H
#include "pomodoro.h"
#include "app_model.h"
#include "sound.h"

typedef enum { SCREEN_TIMER, SCREEN_SETTINGS, SCREEN_NEARBY } app_screen_t;

typedef struct {
    pomodoro_t       pomo;
    app_settings_t   settings;
    neighbor_table_t neighbors;
    app_screen_t     screen;
    bool             alarm_active;   /* focus ended, waiting for dismiss */
    sound_player_t   sound;
} app_t;

app_t *app(void);                    /* the single shared instance */
void   app_init(void);               /* build screens + start tick timer + load timer screen */
void   app_goto(app_screen_t s);     /* switch screens with a slide anim */

/* Play `id` in the current theme at the current volume, pre-empting anything
   already sounding. app_sound_stop() silences immediately and idles the codec. */
void   app_sound(sound_id_t id);
void   app_sound_stop(void);
#endif
```

- [ ] **Step 3: Wire the sequencer into `src/app/app.c`**

Add `#include "sound.h"` to the include block. Add these statics next to `s_next_lux`:

```c
static uint32_t s_next_tick_ms;    /* next focus tick (0 = none scheduled) */
static uint32_t s_alarm_next_ms;   /* next alarm re-ring while un-dismissed */
```

Add, above `route_softkey`:

```c
/* Re-ring the un-acknowledged focus-end alarm on this cadence (the spec calls
   for ring-until-acknowledged; each ring is a one-shot sequence). */
#define ALARM_REPEAT_MS 5000u
#define FOCUS_TICK_MS   60000u

void app_sound(sound_id_t id) {
    sound_play(&s_app.sound, s_app.settings.theme, id, s_app.settings.volume, hal_now_ms());
}

void app_sound_stop(void) {
    sound_reset(&s_app.sound);
    hal_audio_idle();
}

/* 20 ms cadence: note durations are 40-400 ms, far finer than the 200 ms app tick. */
static void sound_cb(lv_timer_t *t) {
    (void)t;
    uint32_t now = hal_now_ms();
    bool was_active = sound_active(&s_app.sound);
    sound_note_t n;
    while (sound_next(&s_app.sound, now, &n)) hal_tone(n.hz, n.ms, n.amp);
    if (was_active && !sound_active(&s_app.sound)) hal_audio_idle();
}
```

Note the `while` (not `if`): a long LVGL flush can delay this callback past several short notes; draining keeps the sequence from stretching. Each iteration overwrites the previous `hal_tone`, which is correct — the skipped notes are already late.

Add a blip to the physical-button path — replace `route_softkey`'s body's first line with a blip, i.e.:

```c
static void route_softkey(int col) {
    app_sound(SND_BLIP);
    switch (s_app.screen) {
        case SCREEN_TIMER:    screen_timer_softkey(col);    break;
        case SCREEN_SETTINGS: screen_settings_softkey(col); break;
        case SCREEN_NEARBY:   screen_nearby_softkey(col);   break;
    }
}
```

In `tick_cb`, replace the two placeholder `hal_tone` calls and add the tick/alarm logic:

```c
    /* advance the pomodoro; surface focus-end as an alarm state */
    pm_event_t ev = pomodoro_tick(&s_app.pomo, now);
    if (ev == PM_EV_FOCUS_ENDED) {
        s_app.alarm_active = true;
        app_sound(SND_FOCUS_END);
        s_alarm_next_ms = now + ALARM_REPEAT_MS;
        s_next_tick_ms = 0;
    }
    if (ev == PM_EV_BREAK_ENDED) app_sound(SND_BREAK_END);

    /* ring until acknowledged */
    if (s_app.alarm_active && (int32_t)(now - s_alarm_next_ms) >= 0) {
        app_sound(SND_FOCUS_END);
        s_alarm_next_ms = now + ALARM_REPEAT_MS;
    }

    /* optional quiet focus tick, once a minute while focus actually runs */
    if (s_app.settings.focus_tick && s_app.pomo.state == PM_FOCUS && !s_app.alarm_active) {
        if (s_next_tick_ms == 0) s_next_tick_ms = now + FOCUS_TICK_MS;
        else if ((int32_t)(now - s_next_tick_ms) >= 0) {
            app_sound(SND_TICK);
            s_next_tick_ms = now + FOCUS_TICK_MS;
        }
    } else {
        s_next_tick_ms = 0;
    }
```

In `app_init`, initialize the new state and start the sound timer — add after `s_app.screen = SCREEN_TIMER; s_app.alarm_active = false;`:

```c
    sound_reset(&s_app.sound);
    s_next_tick_ms = 0; s_alarm_next_ms = 0;
```

and after the existing `lv_timer_create(tick_cb, 200, NULL);`:

```c
    lv_timer_create(sound_cb, 20, NULL);
```

- [ ] **Step 4: Blip on touch softkeys (`src/ui/ui.c`)**

`src/ui/ui.c` currently starts with `#include "ui.h"` then `#include <stdint.h>`, and its softkey handler is at lines 6–11. Add `#include "app.h"` after the `<stdint.h>` line, and rewrite `sk_event` to exactly this (the only change is the inserted `app_sound` line):

```c
static void sk_event(lv_event_t *e) {
    ui_softkey_cb_t cb = (ui_softkey_cb_t)lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target_obj(e);
    int col = (int)(intptr_t)lv_obj_get_user_data(btn);
    app_sound(SND_BLIP);
    if (cb) cb(col);
}
```

The blip is emitted **before** `cb(col)` so that a handler which starts a louder sound (the start chime) pre-empts it — one sound, not two. This one place covers touch presses on all three screens; `route_softkey` in `app.c` covers the physical buttons.

- [ ] **Step 5: Start chime + alarm dismiss (`src/ui/screen_timer.c`)**

In `screen_timer_softkey`, add the start chime and silence the alarm on dismiss:

```c
void screen_timer_softkey(int col) {
    app_t *a = app();
    uint32_t now = hal_now_ms();
    if (a->alarm_active) {
        if (col==0) { pomodoro_acknowledge(&a->pomo, now); a->alarm_active=false; app_sound_stop(); }
        else if (col==4) app_goto(SCREEN_SETTINGS);
        return;
    }
    switch (a->pomo.state) {
        case PM_IDLE:
            if (col==0) { pomodoro_start_focus(&a->pomo, now); app_sound(SND_START); }
            else if (col==3) app_goto(SCREEN_NEARBY);
            else if (col==4) app_goto(SCREEN_SETTINGS);
            break;
```

Leave the `PM_PAUSED` and `default` cases exactly as they are.

- [ ] **Step 6: Run the host suite**

Run: `powershell -File tools/test.ps1`
Expected: 9/9 pass.

- [ ] **Step 7: Build the device firmware**

Run: `powershell -File tools/build.ps1 -Clean`
Expected: PASS, zero warnings, no RAM overflow.

- [ ] **Step 8: Build and smoke-launch the simulator**

Run:
```
cmake -G Ninja -B build-sim -S src/sim -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe -DCMAKE_PREFIX_PATH=C:/msys64/mingw64
cmake --build build-sim
```
Expected: builds clean. Then launch `build-sim/wilidoro_sim.exe` for a few seconds and confirm it does not crash (the sim's `hal_tone` is a no-op, so there is nothing to hear — that is by design). Kill it; do not leave it running.

- [ ] **Step 9: Commit**

```bash
git add src/app/app.h src/app/app.c src/ui/ui.c src/ui/screen_timer.c CMakeLists.txt src/sim/CMakeLists.txt
git commit -m "feat(app): per-theme sounds on start, alarm, break-end, tick and softkeys

A 20 ms LVGL timer drains the sound sequencer into hal_tone; the focus-end
alarm re-rings every 5 s until dismissed, the optional focus tick fires once a
minute, and softkey presses (touch and physical) blip. Volume comes from
app_settings_t.volume. Sim audio stays a no-op.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 4: Document the audio tuning and the on-device listening checklist

**Files:**
- Modify: `docs/hardware-notes.md`

**Interfaces:**
- Consumes: the constants landed in Tasks 1–3 (`TONE_AMP_CAP`, `TONE_FRAMES`, `AUDIO_FS_HZ`, `AUDIO_IDLE_MS`, `SOUND_HZ_MIN`).
- Produces: nothing code-facing.

- [ ] **Step 1: Append the audio section to `docs/hardware-notes.md`**

Insert this **before** the trailing `---` / "Why this note isn't in the BSP" block, so that closing note stays last:

```markdown
## NAU88C10 audio — amplitude ceiling and tone grid

The FreeWili 2's onboard speaker is **0.5 W** (wilibsp AGENTS.md invariant 10),
and the BSP brings the codec up at full scale: DAC volume `0x0b = 0x00ff` and
speaker volume `0x36 = 0x3F` with the 5 V boost on. The only lever an app has
without editing the pinned submodule is the **digital amplitude** it writes into
the I2S stream.

Wilidoro therefore scales every tone by `TONE_AMP_CAP = 160` of 255 in
`src/hal/hal_target.c`. `tone_gen_fill` peaks at `28000/32768` (0.854 FS), so the
ceiling is about **0.54 full scale** — roughly 29 % of full-scale sine power.
Scaling (not clamping) keeps the sound table's relative dynamics intact: a quiet
40-amp blip stays proportionally quieter than a 215-amp alarm note. This is a
bench-comfort default in the same spirit as `LED_BRIGHT_MAX = 40`; it has **not**
yet been level-checked on real hardware.

The output stage is powered down (`codec_nau88c10_speaker_low_power()`) after
`AUDIO_IDLE_MS = 1500` of silence, so the speaker does not idle-hiss between
pomodoros; the next tone wakes it with `dac_mute(false)` + `set_output(SPEAKER)`.

**Tone frequency grid.** One-shot tones ride the BSP's looping DMA read-ring, so
the buffer must be a power-of-two bytes and aligned to its size: wilidoro uses
1024 frames = 4096 B. To make the loop seam inaudible, each tone is snapped to a
whole number of sine cycles in that buffer, which quantizes pitch to
`fs/1024 = 16009/1024 ≈ 15.6 Hz`. Notes below ~440 Hz would detune audibly (and
the small speaker cannot reproduce them), so `SOUND_HZ_MIN = 440` in
`src/app/sound.h` is a hard floor for the sound tables.

### On-device listening checklist (pending — needs a flash session)

Everything in Plan C2 is host-tested and build-verified; **nothing has been
heard**. When the user green-lights a flash:

1. RTT shows `audio: codec ok` at boot (if it says `ABSENT`, `hal_caps().audio`
   is false and all tones are suppressed — check `codec_nau88c10_input_ok()`,
   which gates on the ADC path's `reg 0x02 == 0x0015` as well as the silicon
   revision, and relax it to a revision-only probe if the mic path differs).
2. Press **Start** on each of the three themes — the start chime should be
   distinct per theme, with no click at note boundaries and no buzz.
3. Set focus to 5 min, let it expire — the alarm should re-ring every 5 s until
   **Dismiss**, then stop immediately.
4. Settings → Volume 0 should be fully silent; 50 audibly quieter than 100.
5. Enable the focus tick and confirm one quiet tick a minute, none while paused.
6. After ~2 s of silence the speaker should go quiet with no residual hiss.
7. Confirm the display still flushes smoothly while a tone plays (the I2S TX DMA
   and the ST7796 flush share the DMA block but not an IRQ line).
```

- [ ] **Step 2: Verify the whole branch one more time**

Run, in order:
```
powershell -File tools/test.ps1
powershell -File tools/build.ps1 -Clean
cmake --build build-sim
```
Expected: 9/9 host tests pass; `build/wilidoro.uf2` links with zero warnings; the sim builds.

- [ ] **Step 3: Commit**

```bash
git add docs/hardware-notes.md
git commit -m "docs: record the audio amplitude ceiling, tone grid and listening checklist

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Out of scope (do not do in C2)

- Mic / `audio_capture` / VU metering — playback only.
- Sampled audio assets or `audio_i2s_duplex_play_stream_loop` (needs ≥ 8192-frame multiples = 32 KB+).
- Headphone-jack routing (`CODEC_OUT_HEADPHONE`) and a Settings toggle for it.
- The spec's "in a dark room the alarm volume steps down one notch" — an ambient↔audio cross-feature; log it as backlog, do not build it.
- Crossed-out feature icons on the timer face (existing backlog item; `hal_caps().audio` is now correct for whoever builds them).
- C3 (IMU gestures) and C4 (radio beacon).
