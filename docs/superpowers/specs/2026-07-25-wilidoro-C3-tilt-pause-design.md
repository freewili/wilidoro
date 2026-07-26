# Wilidoro Plan C3 — Tilt to pause (IMU)

*Design approved 2026-07-25. Supersedes the five-gesture table in
`2026-07-24-wilidoro-pomodoro-design.md` — see "Relationship to the original
design" below.*

Plan C3 gives the BMI323 accelerometer exactly one job: **the board lying
face-up and level is the running orientation.** Tilt it out of level — tip it
onto an edge, stand it up, turn it over — and the focus session pauses; set it
back down flat and it resumes.

## 1. Behaviour

One rule, off by default, enabled by a Settings toggle:

- The board **lying face-up and level** is the *running* orientation.
- **Tilt it out of level** while a focus session runs → the session **pauses**
  (the gate reads only the direction of gravity, so picking the board up and
  holding it level does not pause it).
- **Set it back down flat** while paused-from-focus → the session **resumes**.
- Breaks are never gated. Idle is never started. Nothing else changes.

**Edge-triggered, not level-enforced.** This is the load-bearing detail. The app
acts on the *transition* into a zone, never on the standing zone. Consequences:

- Pressing Pause with the board sitting flat is **not** instantly undone by the
  gesture.
- Starting a focus session while the board is propped up does **not**
  immediately pause it.
- Manual control always wins until the board physically moves.

### Thresholds

Hysteresis plus a hold time, so a bump or a reach across the desk does not
count as a lift.

| | value | meaning |
|---|---|---|
| enter flat | `az/‖a‖ ≥ 0.87` | within ~29.5° of level |
| leave flat | `az/‖a‖ ≤ 0.77` | beyond ~39.7° |
| dead band | 29.5°–39.7° | zone unchanged |
| hold | 600 ms | the new zone must persist before it counts |

Dividing by `‖a‖` rather than testing `az` raw is what makes the rule reject
motion: a 2 g jolt can push raw `az` past 0.87 while the board is nowhere near
level. Samples with `‖a‖ < 0.3` (freefall, or a garbled read) are discarded
without touching the hold timer.

All four values are bench-tunable and belong in `docs/hardware-notes.md` once
they have been felt on real hardware, in the same spirit as `LED_BRIGHT_MAX` and
`TONE_AMP_CAP`.

## 2. Modules and data flow

```
BMI323 ──hal_imu()──▶ core/tilt.c ──TILT_EV_FLAT/LIFTED──▶ app.c ──▶ pomodoro_pause/resume
 (I2C1)               (pure, host-tested)                  (gated)
```

### `src/core/tilt.h` / `tilt.c` — new, pure

Replaces `src/core/gestures.h` / `gestures.c`. No LVGL, no hardware, host-tested.

```c
typedef enum { TILT_EV_NONE, TILT_EV_FLAT, TILT_EV_LIFTED } tilt_event_t;
typedef struct { int8_t zone, cand; uint32_t cand_since_ms; bool primed; } tilt_state_t;
void         tilt_init(tilt_state_t *t);
tilt_event_t tilt_feed(tilt_state_t *t, float ax, float ay, float az, uint32_t now_ms);
```

`zone` is the committed orientation (`+1` flat, `-1` lifted, `0` before priming);
`cand` is a pending change being timed against `cand_since_ms`, with `0` meaning
"nothing pending". A sample that returns to the committed zone clears `cand`.
All time comparisons use wrap-safe `(int32_t)(now - deadline) >= 0`.

The first valid sample **primes** the zone silently — it adopts the current
orientation and emits nothing. This is what stops the Settings toggle from
firing a spurious event when it is switched on.

### `hal_imu` on device (`src/hal/hal_target.c`)

`bmi323_init()` in `hal_init` alongside the existing `opt4001_init()`;
`hal_caps().imu` reflects the result; `hal_imu()` wraps `bmi323_read()`, keeping
the three accelerometer axes and discarding the gyro. The BSP's burst read always
fetches all six axes — not worth forking the driver to avoid three registers.

### `hal_imu` in the simulator (`src/hal/hal_sim.c`)

Replaces the hardcoded `az = 1.0`. `-` / `=` tilt a fake board in 15° steps,
clamped to 0°–90° and starting at 0° (flat), exactly as `[` / `]` already vary
fake lux, emitting `ax = sin θ, az = cos θ`. Stepping rather than snapping
between two states means the simulator genuinely exercises the hysteresis band
and the hold timer.

## 3. Cadence — one owner for I2C1

The OPT4001 (Plan C1), the BMI323, and the NAU88C10's control registers all sit
on **I2C1**. Every BSP call is blocking, but they all run from LVGL timers on
core 0, so this is a **time-budget** question, not a locking one.

A single new `sensor_cb` LVGL timer at **100 ms** performs every I2C1 sensor
read: the IMU on each call, lux every fifth. **The lux poll moves out of
`tick_cb`.** This gives one owner for the shared bus rather than two independent
pollers, and trims the accreting `tick_cb` (a logged backlog item).

- 100 ms → 6 samples per 600 ms hold window.
- A `bmi323_read` is 1 byte written + 14 read ≈ 0.35 ms of bus time, so ~0.4 %
  duty at 10 Hz.
- With the toggle off, `hal_imu` is not called at all — zero added bus traffic.

The lux deadline also becomes wrap-safe (`(int32_t)(now - s_next_lux) >= 0`) to
match every other deadline in `app.c`, since the line is moving anyway.

This move is not quite cadence-neutral. The 500 ms deadline used to be sampled
by `tick_cb`'s 200 ms timer, so the real mean interval between lux reads was
~600 ms; sampled by `sensor_cb`'s 100 ms timer it is a true 500 ms. `dim_apply`
is a fixed-α EMA (`DIM_ALPHA = 0.25`), so its wall-clock time constant drops
from ~2.1 s to ~1.75 s — auto-dim now converges about 17 % faster. Benign, and
arguably closer to the "~2 Hz" this section already claimed, but worth knowing
before retuning `DIM_ALPHA`.

## 4. Settings and UI

- `bool tilt_pause` in `app_settings_t`, **default off**.
- A `"Tilt to pause"` row, added with the existing `add_row()` helper.
- `app_tilt_apply()` mirrors `app_dvi_apply()` and re-primes the tilt state
  (`tilt_init`) when the toggle flips on. `app_init` calls `tilt_init` once at
  startup alongside `dim_init`.
- Following the precedent of the inert `Beacon` row, the row always exists — but
  its value reads `"no imu"` instead of `"off"` when `hal_caps().imu` is false,
  so a dead sensor is visible rather than silent.
- A `SND_BLIP` on each gesture-driven pause and resume, same as a softkey press.
  Cheap, and it makes a spurious trigger audible rather than something
  discovered ten minutes later.

## 5. Error handling

- `bmi323_init()`'s return value reflects only whether the chip-ID read got a
  bus ACK, not whether the ID matched — a wrong ID is DIAG-logged as `??` but
  still leaves `caps.imu` true. Only a bus NAK (no ACK at all) clears
  `caps.imu` and makes `hal_imu()` return false: only then is the feature
  inert and the Settings row says so.
- A transient `bmi323_read` failure skips that sample. Wrap-safe subtraction
  means the resulting gap in the hold timer is harmless.
- `pomodoro_pause()` and `pomodoro_resume()` are already guarded no-ops outside
  their valid states. The app-layer gates (`state == PM_FOCUS` to pause,
  `state == PM_PAUSED && resume_state == PM_FOCUS` to resume) are belt-and-braces
  on top of that, and are what confine the rule to focus sessions.
- `hal_caps().imu` is latched once in `hal_init` and never re-checked. A BMI323
  that dies at runtime is invisible: `sensor_cb` silently stops feeding the
  gate, an existing tilt-pause persists until the user presses Resume by hand,
  and Settings keeps reading `on`.
- Pressing **Default** while a session is tilt-paused strands the pause:
  Default sets `tilt_pause = false` and re-primes, so the gate goes inert and
  setting the board back down will not resume it. This is correct given
  Default's existing semantics (identical to `dvi_on`), but it is the one
  sequence where the gate pauses a session and then cannot un-pause it.

## 6. Testing

**Host (`tests/test_tilt.c`, replacing `test_gestures.c` in all three build
lists — root `CMakeLists.txt`, `src/sim/CMakeLists.txt`, `tests/CMakeLists.txt`;
the suite stays at 10 binaries):**

- The hold being satisfied emits `TILT_EV_LIFTED` exactly once, and not one
  sample early.
- Returning to flat mid-hold cancels the pending change with no event.
- A sample inside the dead band leaves the zone unchanged and emits nothing.
- A 2 g jolt whose raw `az` exceeds 0.87 is rejected by the `‖a‖` normalization.
- `‖a‖ ≈ 0` (freefall) is discarded.
- The first sample primes the zone silently.

**Simulator:** tilt with `-` / `=` and watch a focus session pause and resume;
confirm a manual pause while flat is not immediately undone.

**Hardware:** a checklist appended to `docs/hardware-notes.md` — `bmi323: chipid`
in RTT, a physical lift pauses focus, setting it down resumes, the toggle off is
inert. **To be run only with the user's explicit say-so**, per the standing rule
that flashing, RTT, the probe, the camera and the microphone all need asking
first.

## 7. Out of scope

Shake-to-dismiss, pick-up-to-wake, deep-focus (screen/LED blanking), idle-timeout
dimming, and the gyro. Plan C4 (the CC1101 beacon) remains untouched.

## 8. Relationship to the original design

`2026-07-24-wilidoro-pomodoro-design.md` specifies five gesture→action mappings
(flip-down starts focus, flip-down enters deep-focus, flip-up restores, shake
dismisses the alarm, pick-up wakes a dimmed screen). **That table is superseded
by the single rule in this document.** The reasons, recorded so the decision is
not re-litigated:

- Deep-focus and pick-up-to-wake were dropped as not worth the complexity.
  Pick-up in particular had nothing to act on: the auto-dim in Plan C1 is purely
  ambient-light-driven, so no screen ever dims from inactivity, and the gesture
  would have required a new idle-timeout state.
- Shake-to-dismiss was dropped as unwanted.
- What remains — "is it lying flat?" — is a *continuous orientation state*, not a
  discrete gesture, which is why `core/gestures.c` is replaced rather than reused.

`core/gestures.c` has had **no consumer** since Plan A; only its own test binary
and three build lists reference it. Two further reasons not to build on it:

1. Its thresholds are `az ≤ −0.6` / `az ≥ +0.6`, and `face` only changes when
   one is crossed. Held upright at `az ≈ 0`, *neither* fires — `face` keeps its
   stale value and no event is emitted. It structurally cannot detect "lifted".
2. It has a latent event-masking bug: `gesture_feed` sets `face_reported = true`
   unconditionally but emits the flip only `if (out == GEV_NONE)`. A real flip
   sweeps `az` from +1 to −1, easily exceeding the 2.5 summed-jerk shake
   threshold in the same sample, which would consume the flip **permanently**.
   Replacing the module removes this rather than patching it.
