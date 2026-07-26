# Wilidoro Plan C4 — CC1101 focus beacon: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Broadcast this wilidoro's focus state over the CC1101 sub-GHz radio and listen for other wilidoros, populating the Nearby screen.

**Architecture:** The pure codec (`core/beacon.c`) and neighbour table already exist and are host-tested. This plan adds the one missing piece of pure logic — `core/beacon_rx.c`, a streaming framer that cuts a raw OOK edge stream into frames `beacon_ook_decode` will accept — plus `app_beacon_msg` to build the outgoing message, real `hal_beacon_tx`/`hal_beacon_rx` over the BSP's CC1101 driver, a boot loopback self-test, and a 20 s transmit cadence gated on audio being idle. **No change to the LVGL flush path.**

**Tech Stack:** C11, LVGL 9, the `wilibsp` BSP (`cc1101_*`, `ook_tx_send`, `gdo_capture_*`, `ioexp_antenna`), greatest.h for host tests, CMake + Ninja, SDL2 for the simulator.

**Spec:** `docs/superpowers/specs/2026-07-26-wilidoro-C4-beacon-design.md`

## Global Constraints

- **The HAL is the only hardware seam.** `src/app/`, `src/ui/`, `src/core/` must never include a `wilibsp` or SDL header. Only `src/hal/hal_target.c` and `src/hal/hal_sim.c` may. The HAL *may* call `core/` freely.
- **Pure logic is host-tested.** `src/core/` and `src/app/app_model.c` stay LVGL-free and hardware-free so CTest can cover them.
- **The host suite goes from 10 binaries to 11** and must pass 11/11. Run: `powershell -File tools/test.ps1`
- **The device build must be zero-warnings with no RAM overflow.** Run: `powershell -File tools/build.ps1 -Clean` (~10 min; pass `timeout: 600000`). Report the linker's **memory-region usage table**, not `.elf`/`.uf2` file sizes. Baseline before this plan: bss 441,396 of 532,480 bytes.
- **Simulator build:** `cmake --build build-sim` (already configured; do not reconfigure). Zero warnings, must link.
- **Host tests compile with `-Wall -Wextra`.** No unused-parameter or sign-compare warnings.
- **All time comparisons must be wrap-safe:** `(int32_t)(now - deadline) >= 0`, never `now >= deadline`.
- **`wilibsp/` is a pinned submodule — do not modify it.**
- **Do NOT modify `src/hal/hal.h`.** `hal_beacon_tx` and `hal_beacon_rx` are already declared there (lines 48–49) with the right signatures.
- **Do NOT touch the LVGL flush path** (`src/target/lvgl_port.c`, `st7796_blit_rect`). The spec explains at length why the async migration is not needed.
- **NEVER touch hardware without asking the user first** — flashing, OpenOCD/RTT, the debug probe, the camera, the microphone. Builds, host tests and the simulator are free.
- **Commit trailer on every commit:**
  ```
  Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
  ```

## File Structure

| File | Responsibility |
|---|---|
| `src/core/beacon_rx.h` (create) | `beacon_rx_t`, `BEACON_GAP_US`, two function decls |
| `src/core/beacon_rx.c` (create) | The streaming burst framer — the only new logic in this plan |
| `tests/test_beacon_rx.c` (create) | 7 host tests for the framer |
| `tests/CMakeLists.txt`, `CMakeLists.txt`, `src/sim/CMakeLists.txt` (modify) | Add `beacon_rx.c` / the new test binary |
| `src/app/app_model.h`, `app_model.c` (modify) | `app_beacon_msg` — app state → `beacon_msg_t` |
| `tests/test_app_model.c` (modify) | Cover the state mapping and both byte clamps |
| `src/hal/hal_target.c` (modify) | Radio bring-up, real `hal_beacon_tx`/`hal_beacon_rx`, `caps.radio`, boot loopback self-test |
| `src/app/app.c` (modify) | 20 s transmit cadence gated on `beacon_on` + audio idle; RX drain moves from `tick_cb` to `sensor_cb` |
| `docs/hardware-notes.md` (modify) | Radio notes + the on-device checklist |

**Reference facts you will need** (already verified; do not re-derive):

- A frame is `(BEACON_PREAMBLE_BITS + BEACON_WIRE_LEN * 8) = 8 + 128 = 136` bits, each two half-bits of `BEACON_HALFBIT_US = 500`, so **136 ms per transmit**.
- `BEACON_MAX_DURS` = `8*2 + 16*8*2 + 4` = **276**.
- `beacon_ook_decode` assumes `durs[0]` is a **HIGH** run, rejects any run longer than **4 half-bits**, and has no framing search.
- `sound_active(const sound_player_t *p)` already exists in `src/app/sound.h`.
- `src/app/app_model.h` already includes `pomodoro.h` (line 6), so `pm_state_t` is in scope there.

---

### Task 1: `core/beacon_rx` — the streaming burst framer

The only new logic in this plan. Pure, host-tested, no hardware, no LVGL.

**Files:**
- Create: `src/core/beacon_rx.h`, `src/core/beacon_rx.c`, `tests/test_beacon_rx.c`
- Modify: `tests/CMakeLists.txt` (add a binary), `CMakeLists.txt`, `src/sim/CMakeLists.txt`

**Interfaces:**
- Consumes: `beacon_ook_decode`, `beacon_pack`, `beacon_ook_encode`, `BEACON_WIRE_LEN`, `BEACON_MAX_DURS`, `BEACON_HALFBIT_US`, `beacon_msg_t`, `beacon_state_t` — all existing, from `src/core/beacon.h`.
- Produces: `BEACON_GAP_US`; `beacon_rx_t`; `void beacon_rx_init(beacon_rx_t *r)`; `bool beacon_rx_push(beacon_rx_t *r, uint32_t dur_us, uint8_t out[BEACON_WIRE_LEN])`. Task 4 calls exactly these.

- [ ] **Step 1: Write the header**

Create `src/core/beacon_rx.h`:

```c
// src/core/beacon_rx.h -- cut a raw OOK edge stream into decodable beacon frames.
#ifndef WILIDORO_BEACON_RX_H
#define WILIDORO_BEACON_RX_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "beacon.h"

/* beacon_ook_decode rejects any run longer than 4 half-bits, so 5 is the
   smallest unambiguous "this is the inter-frame gap" threshold. */
#define BEACON_GAP_US (BEACON_HALFBIT_US * 5u)

typedef struct {
    uint32_t durs[BEACON_MAX_DURS];
    size_t   n;
    bool     armed;   /* a gap has been seen, so the next run is known to be high */
} beacon_rx_t;

void beacon_rx_init(beacon_rx_t *r);

/* Feed one captured duration (microseconds the line held its current level).
   Returns true exactly when a frame decoded, writing the wire frame to `out`. */
bool beacon_rx_push(beacon_rx_t *r, uint32_t dur_us, uint8_t out[BEACON_WIRE_LEN]);
#endif
```

- [ ] **Step 2: Write the failing tests**

Create `tests/test_beacon_rx.c`:

```c
#include "greatest.h"
#include "beacon_rx.h"
#include <string.h>

static void make_wire(uint8_t wire[BEACON_WIRE_LEN], const char *name8,
                      beacon_state_t st, uint8_t mins, uint8_t done) {
    beacon_msg_t m;
    memcpy(m.name, name8, BEACON_NAME_LEN);
    m.state = st; m.minutes_left = mins; m.completed = done;
    beacon_pack(&m, wire);
}

/* Push a run of durations; true if any single push reported a decoded frame. */
static bool push_all(beacon_rx_t *r, const uint32_t *durs, size_t n,
                     uint8_t out[BEACON_WIRE_LEN]) {
    bool got = false;
    for (size_t i = 0; i < n; i++) if (beacon_rx_push(r, durs[i], out)) got = true;
    return got;
}

/* A gap comfortably longer than the threshold, as a real idle period would be. */
#define GAP (BEACON_GAP_US * 4u)

TEST round_trip_through_the_framer(void) {
    uint8_t wire[BEACON_WIRE_LEN]; make_wire(wire, "ALEX    ", BST_FOCUS, 17, 2);
    uint32_t durs[BEACON_MAX_DURS]; bool lvl = false;
    size_t n = beacon_ook_encode(wire, durs, BEACON_MAX_DURS, &lvl);
    ASSERT(n > 0);
    ASSERT(lvl);                       /* the preamble starts high */

    beacon_rx_t r; beacon_rx_init(&r);
    uint8_t got[BEACON_WIRE_LEN];
    ASSERT_FALSE(beacon_rx_push(&r, GAP, got));   /* leading gap arms the framer */
    ASSERT_FALSE(push_all(&r, durs, n, got));     /* nothing decodes mid-burst */
    ASSERT(beacon_rx_push(&r, GAP, got));         /* the trailing gap closes it */
    ASSERT_MEM_EQ(wire, got, BEACON_WIRE_LEN);
    PASS();
}

TEST merged_trailing_halfbit_still_decodes(void) {
    /* Find a frame whose final data bit is 1. Manchester encodes 1 as {high,low},
       so its trailing low half-bit is exactly what ook_tx_send merges into the
       idle gap -- it never reaches the capture. The final data bit is the LSB of
       the CRC low byte, the last wire byte. */
    uint8_t wire[BEACON_WIRE_LEN];
    int done = -1;
    for (int c = 0; c < 256; c++) {
        make_wire(wire, "ALEX    ", BST_FOCUS, 17, (uint8_t)c);
        if (wire[BEACON_WIRE_LEN - 1] & 1u) { done = c; break; }
    }
    ASSERT(done >= 0);
    make_wire(wire, "ALEX    ", BST_FOCUS, 17, (uint8_t)done);

    uint32_t durs[BEACON_MAX_DURS]; bool lvl = false;
    size_t n = beacon_ook_encode(wire, durs, BEACON_MAX_DURS, &lvl);
    ASSERT(n > 1);

    beacon_rx_t r; beacon_rx_init(&r);
    uint8_t got[BEACON_WIRE_LEN];
    beacon_rx_push(&r, GAP, got);
    push_all(&r, durs, n - 1, got);               /* drop the merged final low run */
    ASSERT(beacon_rx_push(&r, GAP, got));
    ASSERT_MEM_EQ(wire, got, BEACON_WIRE_LEN);
    PASS();
}

TEST clean_tail_decodes_on_the_first_attempt(void) {
    /* The mirror case: a final data bit of 0 is {low,high}, so the frame ends
       high, the gap terminates it, and the capture is already complete. */
    uint8_t wire[BEACON_WIRE_LEN];
    int done = -1;
    for (int c = 0; c < 256; c++) {
        make_wire(wire, "ALEX    ", BST_FOCUS, 17, (uint8_t)c);
        if ((wire[BEACON_WIRE_LEN - 1] & 1u) == 0u) { done = c; break; }
    }
    ASSERT(done >= 0);
    make_wire(wire, "ALEX    ", BST_FOCUS, 17, (uint8_t)done);

    uint32_t durs[BEACON_MAX_DURS]; bool lvl = false;
    size_t n = beacon_ook_encode(wire, durs, BEACON_MAX_DURS, &lvl);

    beacon_rx_t r; beacon_rx_init(&r);
    uint8_t got[BEACON_WIRE_LEN];
    beacon_rx_push(&r, GAP, got);
    push_all(&r, durs, n, got);
    ASSERT(beacon_rx_push(&r, GAP, got));
    ASSERT_MEM_EQ(wire, got, BEACON_WIRE_LEN);
    PASS();
}

TEST durations_before_the_first_gap_are_discarded(void) {
    uint8_t wire[BEACON_WIRE_LEN]; make_wire(wire, "ALEX    ", BST_FOCUS, 17, 2);
    uint32_t durs[BEACON_MAX_DURS]; bool lvl = false;
    size_t n = beacon_ook_encode(wire, durs, BEACON_MAX_DURS, &lvl);

    beacon_rx_t r; beacon_rx_init(&r);
    uint8_t got[BEACON_WIRE_LEN];
    /* No leading gap: polarity is unknown, so nothing may accumulate. */
    ASSERT_FALSE(push_all(&r, durs, n, got));
    ASSERT_FALSE(beacon_rx_push(&r, GAP, got));   /* empty segment, no decode */
    /* Now armed, the very next clean burst decodes. */
    ASSERT_FALSE(push_all(&r, durs, n, got));
    ASSERT(beacon_rx_push(&r, GAP, got));
    ASSERT_MEM_EQ(wire, got, BEACON_WIRE_LEN);
    PASS();
}

TEST two_frames_back_to_back_both_decode(void) {
    uint8_t w1[BEACON_WIRE_LEN], w2[BEACON_WIRE_LEN];
    make_wire(w1, "ALEX    ", BST_FOCUS, 17, 2);
    make_wire(w2, "BEA     ", BST_BREAK, 4, 9);
    uint32_t d1[BEACON_MAX_DURS], d2[BEACON_MAX_DURS]; bool lvl = false;
    size_t n1 = beacon_ook_encode(w1, d1, BEACON_MAX_DURS, &lvl);
    size_t n2 = beacon_ook_encode(w2, d2, BEACON_MAX_DURS, &lvl);

    beacon_rx_t r; beacon_rx_init(&r);
    uint8_t got[BEACON_WIRE_LEN];
    beacon_rx_push(&r, GAP, got);
    push_all(&r, d1, n1, got);
    ASSERT(beacon_rx_push(&r, GAP, got));
    ASSERT_MEM_EQ(w1, got, BEACON_WIRE_LEN);
    push_all(&r, d2, n2, got);
    ASSERT(beacon_rx_push(&r, GAP, got));
    ASSERT_MEM_EQ(w2, got, BEACON_WIRE_LEN);
    PASS();
}

TEST garbage_between_frames_does_not_block_the_second(void) {
    uint8_t w1[BEACON_WIRE_LEN], w2[BEACON_WIRE_LEN];
    make_wire(w1, "ALEX    ", BST_FOCUS, 17, 2);
    make_wire(w2, "BEA     ", BST_BREAK, 4, 9);
    uint32_t d1[BEACON_MAX_DURS], d2[BEACON_MAX_DURS]; bool lvl = false;
    size_t n1 = beacon_ook_encode(w1, d1, BEACON_MAX_DURS, &lvl);
    size_t n2 = beacon_ook_encode(w2, d2, BEACON_MAX_DURS, &lvl);

    beacon_rx_t r; beacon_rx_init(&r);
    uint8_t got[BEACON_WIRE_LEN];
    beacon_rx_push(&r, GAP, got);
    push_all(&r, d1, n1, got);
    ASSERT(beacon_rx_push(&r, GAP, got));
    ASSERT_MEM_EQ(w1, got, BEACON_WIRE_LEN);

    /* A short burst of nonsense, closed by its own gap: too few half-bits. */
    for (int i = 0; i < 9; i++) beacon_rx_push(&r, BEACON_HALFBIT_US * 3u, got);
    ASSERT_FALSE(beacon_rx_push(&r, GAP, got));

    push_all(&r, d2, n2, got);
    ASSERT(beacon_rx_push(&r, GAP, got));
    ASSERT_MEM_EQ(w2, got, BEACON_WIRE_LEN);
    PASS();
}

TEST overflow_discards_and_recovers(void) {
    beacon_rx_t r; beacon_rx_init(&r);
    uint8_t got[BEACON_WIRE_LEN];
    beacon_rx_push(&r, GAP, got);
    /* Far more runs than a frame can hold: the segment must be dropped, and the
       framer must not wedge. */
    for (size_t i = 0; i < BEACON_MAX_DURS + 10u; i++)
        ASSERT_FALSE(beacon_rx_push(&r, BEACON_HALFBIT_US, got));

    uint8_t wire[BEACON_WIRE_LEN]; make_wire(wire, "BEA     ", BST_BREAK, 4, 9);
    uint32_t durs[BEACON_MAX_DURS]; bool lvl = false;
    size_t n = beacon_ook_encode(wire, durs, BEACON_MAX_DURS, &lvl);
    ASSERT_FALSE(beacon_rx_push(&r, GAP, got));   /* re-arms after the overflow */
    ASSERT_FALSE(push_all(&r, durs, n, got));
    ASSERT(beacon_rx_push(&r, GAP, got));
    ASSERT_MEM_EQ(wire, got, BEACON_WIRE_LEN);
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(round_trip_through_the_framer);
    RUN_TEST(merged_trailing_halfbit_still_decodes);
    RUN_TEST(clean_tail_decodes_on_the_first_attempt);
    RUN_TEST(durations_before_the_first_gap_are_discarded);
    RUN_TEST(two_frames_back_to_back_both_decode);
    RUN_TEST(garbage_between_frames_does_not_block_the_second);
    RUN_TEST(overflow_discards_and_recovers);
    GREATEST_MAIN_END();
}
```

- [ ] **Step 3: Register the new test binary**

In `tests/CMakeLists.txt`, after the existing `add_core_test(test_beacon ${CORE}/beacon.c)` line, add:

```cmake
add_core_test(test_beacon_rx ${CORE}/beacon_rx.c ${CORE}/beacon.c)
```

- [ ] **Step 4: Run the tests to verify they fail**

Run: `powershell -File tools/test.ps1`

Expected: **CMake fails at configure time** — `Cannot find source file: .../src/core/beacon_rx.c`, because the build list now names a file that does not exist. That is the correct failure at this point; do not proceed until you have seen it.

- [ ] **Step 5: Write the implementation**

Create `src/core/beacon_rx.c`:

```c
#include "beacon_rx.h"

void beacon_rx_init(beacon_rx_t *r) {
    beacon_rx_t z = {0};
    *r = z;
}

/* Try the segment as captured, then again with one synthetic low half-bit
   appended. ook_tx_send leaves the line low at the end of a burst, so when the
   last data bit is 1 (Manchester {high,low}) the frame's final low half-bit
   merges into the idle gap and never appears in the capture. Run-count parity
   cannot tell that apart from a frame that legitimately ends high, so both
   spellings get one attempt. */
static bool try_decode(beacon_rx_t *r, uint8_t out[BEACON_WIRE_LEN]) {
    if (r->n == 0) return false;
    if (beacon_ook_decode(r->durs, r->n, out)) return true;
    if (r->n >= BEACON_MAX_DURS) return false;
    r->durs[r->n] = BEACON_HALFBIT_US;
    return beacon_ook_decode(r->durs, r->n + 1, out);
}

bool beacon_rx_push(beacon_rx_t *r, uint32_t dur_us, uint8_t out[BEACON_WIRE_LEN]) {
    if (dur_us >= BEACON_GAP_US) {
        bool got = try_decode(r, out);
        r->n = 0;
        /* Idle is carrier-off, i.e. low, so the run after a gap is high --
           exactly the polarity beacon_ook_decode assumes. */
        r->armed = true;
        return got;
    }
    if (!r->armed) return false;          /* polarity unknown until the first gap */
    if (r->n >= BEACON_MAX_DURS) {        /* noise: drop it and resync on the next gap */
        r->n = 0;
        r->armed = false;
        return false;
    }
    r->durs[r->n++] = dur_us;
    return false;
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `powershell -File tools/test.ps1`

Expected: `100% tests passed, 0 tests failed out of 11`, with `test_beacon_rx` among them. Zero compiler warnings.

- [ ] **Step 7: Prove the retry path is not vacuous**

The `merged_trailing_halfbit_still_decodes` test only earns its keep if it fails without the retry. Temporarily delete these three lines from `try_decode`:

```c
    if (r->n >= BEACON_MAX_DURS) return false;
    r->durs[r->n] = BEACON_HALFBIT_US;
    return beacon_ook_decode(r->durs, r->n + 1, out);
```

and replace them with `return false;`. Run `powershell -File tools/test.ps1`.

Expected: `test_beacon_rx` **FAILS** on `merged_trailing_halfbit_still_decodes` specifically. Then **restore the three lines exactly** and re-run to confirm 11/11. Report both outcomes, and confirm with `git diff src/core/beacon_rx.c` that the weakened version is gone before committing.

- [ ] **Step 8: Add `beacon_rx.c` to the two remaining build lists**

In `CMakeLists.txt` (root), after the existing `    src/core/beacon.c` line, add:

```cmake
    src/core/beacon_rx.c
```

In `src/sim/CMakeLists.txt`, after the existing `    ${CMAKE_CURRENT_SOURCE_DIR}/../core/beacon.c` line, add:

```cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/beacon_rx.c
```

- [ ] **Step 9: Verify the simulator still builds**

Run: `cmake --build build-sim`

Expected: compiles and links cleanly. (Nothing consumes `beacon_rx.c` yet — this only proves the build-list additions are correct.)

- [ ] **Step 10: Commit**

```bash
git add src/core/beacon_rx.h src/core/beacon_rx.c tests/test_beacon_rx.c tests/CMakeLists.txt CMakeLists.txt src/sim/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(core): add the beacon RX burst framer

beacon_ook_decode cannot be fed a raw capture stream: it assumes the
first run is high, rejects any run longer than 4 half-bits, and has no
framing search, so an idle gap alone fails it. beacon_rx cuts the stream
into candidate frames, deriving polarity rather than guessing it -- idle
is carrier-off, so the run after a gap is known to be high.

It also handles a subtlety that costs a frame in every other transmit:
ook_tx_send leaves the line low, so when the last data bit is 1 the
frame's final low half-bit merges into the idle gap and never reaches the
capture. Run-count parity cannot distinguish that from a frame that ends
high, so the framer retries with one synthetic half-bit appended. A
mutation check confirms the merged-tail test fails without that retry.

Overflow drops the segment and clears `armed`, so noise resyncs on the
next gap instead of accumulating with unknown polarity.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: `app_beacon_msg` — app state to wire message

**Files:**
- Modify: `src/app/app_model.h` (declare), `src/app/app_model.c` (implement)
- Modify: `tests/test_app_model.c` (cover it)

**Interfaces:**
- Consumes: `app_settings_t`, `pomodoro_t`, `pomodoro_remaining_ms`, `pm_state_t`, `beacon_msg_t`, `beacon_state_t` — all existing. `app_model.h` already includes both `beacon.h` and `pomodoro.h`.
- Produces: `void app_beacon_msg(const app_settings_t *s, const pomodoro_t *p, uint32_t now_ms, beacon_msg_t *out)`. Task 5 calls exactly this.

- [ ] **Step 1: Write the failing tests**

In `tests/test_app_model.c`, add these three tests immediately before `GREATEST_MAIN_DEFS();`:

```c
TEST beacon_msg_maps_state_and_copies_name(void) {
    app_settings_t s; app_settings_defaults(&s);
    pomodoro_t p; pomodoro_init(&p, (pm_config_t){ .focus_min = 25, .short_min = 5,
                                                   .long_min = 15, .long_every = 4 });
    beacon_msg_t m;

    /* Idle: nothing running. */
    app_beacon_msg(&s, &p, 0, &m);
    ASSERT_EQ(BST_IDLE, m.state);
    ASSERT_MEM_EQ(s.name, m.name, APP_NAME_LEN);   /* verbatim, space-padded */

    /* Focus running. */
    pomodoro_start_focus(&p, 0);
    app_beacon_msg(&s, &p, 0, &m);
    ASSERT_EQ(BST_FOCUS, m.state);
    ASSERT_EQ(25, m.minutes_left);

    /* Paused is NOT focusing -- broadcasting otherwise would freeze a
       neighbour's countdown at a stale figure. */
    pomodoro_pause(&p, 60000u);
    app_beacon_msg(&s, &p, 60000u, &m);
    ASSERT_EQ(BST_IDLE, m.state);

    /* A break reads as a break. */
    pomodoro_resume(&p, 60000u);
    pomodoro_tick(&p, 60000u + 25u * 60000u);      /* focus ends -> PM_ALARM */
    app_beacon_msg(&s, &p, 60000u + 25u * 60000u, &m);
    ASSERT_EQ(BST_IDLE, m.state);                  /* alarm is not focusing either */
    pomodoro_acknowledge(&p, 0);                   /* -> a break */
    app_beacon_msg(&s, &p, 0, &m);
    ASSERT_EQ(BST_BREAK, m.state);
    PASS();
}

TEST beacon_msg_clamps_minutes_to_a_byte(void) {
    app_settings_t s; app_settings_defaults(&s);
    /* A phase far longer than 255 minutes must not wrap the single wire byte. */
    pomodoro_t p; pomodoro_init(&p, (pm_config_t){ .focus_min = 600, .short_min = 5,
                                                   .long_min = 15, .long_every = 4 });
    pomodoro_start_focus(&p, 0);
    beacon_msg_t m;
    app_beacon_msg(&s, &p, 0, &m);
    ASSERT_EQ(255, m.minutes_left);
    PASS();
}

TEST beacon_msg_clamps_completed_to_a_byte(void) {
    app_settings_t s; app_settings_defaults(&s);
    pomodoro_t p; pomodoro_init(&p, (pm_config_t){ .focus_min = 25, .short_min = 5,
                                                   .long_min = 15, .long_every = 4 });
    p.stats.completed = 400;           /* accumulates without bound in a long session */
    beacon_msg_t m;
    app_beacon_msg(&s, &p, 0, &m);
    ASSERT_EQ(255, m.completed);
    PASS();
}
```

Register all three in `main()`, after the existing `RUN_TEST(theme_cycles_0_1_2);` line:

```c
    RUN_TEST(beacon_msg_maps_state_and_copies_name);
    RUN_TEST(beacon_msg_clamps_minutes_to_a_byte);
    RUN_TEST(beacon_msg_clamps_completed_to_a_byte);
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `powershell -File tools/test.ps1`

Expected: the build fails compiling `test_app_model.c` — `implicit declaration of function 'app_beacon_msg'`.

- [ ] **Step 3: Declare it**

In `src/app/app_model.h`, immediately after the existing `void neighbor_upsert(...)` declaration:

```c
/* Build this device's outgoing beacon from live app state. Pure. PM_PAUSED and
   PM_ALARM both report BST_IDLE: a paused session is not focusing, and saying
   otherwise would freeze a neighbour's countdown at a stale figure. */
void app_beacon_msg(const app_settings_t *s, const pomodoro_t *p,
                    uint32_t now_ms, beacon_msg_t *out);
```

- [ ] **Step 4: Implement it**

In `src/app/app_model.c`, at the end of the file:

```c
void app_beacon_msg(const app_settings_t *s, const pomodoro_t *p,
                    uint32_t now_ms, beacon_msg_t *out) {
    memcpy(out->name, s->name, APP_NAME_LEN);
    switch (p->state) {
        case PM_FOCUS:                          out->state = BST_FOCUS; break;
        case PM_BREAK_SHORT: case PM_BREAK_LONG: out->state = BST_BREAK; break;
        default:                                out->state = BST_IDLE;  break;
    }
    /* Both wire fields are single bytes. Minutes round down; completed
       accumulates without bound across a long session. */
    uint32_t mins = pomodoro_remaining_ms(p, now_ms) / 60000u;
    out->minutes_left = (uint8_t)(mins > 255u ? 255u : mins);
    out->completed    = (uint8_t)(p->stats.completed > 255u ? 255u : p->stats.completed);
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `powershell -File tools/test.ps1`

Expected: `100% tests passed, 0 tests failed out of 11`.

- [ ] **Step 6: Verify the simulator still builds**

Run: `cmake --build build-sim`

Expected: compiles and links, zero warnings.

- [ ] **Step 7: Commit**

```bash
git add src/app/app_model.h src/app/app_model.c tests/test_app_model.c
git commit -m "$(cat <<'EOF'
feat(app): build the outgoing beacon from app state

Nothing mapped app state to a beacon_msg_t -- the type was only ever
consumed, on the receive side. app_beacon_msg sits beside neighbor_upsert
because app_model already speaks both beacon_msg_t and pm_state_t, and
because being pure keeps it host-testable.

PM_PAUSED and PM_ALARM both report BST_IDLE. A paused session is not
focusing, and broadcasting it as such would freeze a neighbour's
"focusing, 12 min left" at a stale figure. Both wire fields are single
bytes, so minutes_left and completed clamp at 255 rather than wrapping.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: Radio bring-up and `hal_beacon_tx`

The hardware seam, so nothing here is host-testable; the device build plus Task 4's loopback self-test are the verification.

**Files:**
- Modify: `src/hal/hal_target.c` (includes, state, `hal_init`, `hal_beacon_tx`, `hal_caps`)

**Interfaces:**
- Consumes: `beacon_ook_encode` (from `core/beacon.h`, already included via `hal.h`); BSP `ioexp_antenna`, `cc1101_init`, `cc1101_tx_ook_start`, `cc1101_tx_ook_stop`, `cc1101_monitor_rx`, `ook_tx_send`, `gdo_capture_init`, `gdo_capture_start`, `gdo_capture_attach_pin`.
- Produces: a working `void hal_beacon_tx(const uint8_t wire[BEACON_WIRE_LEN])`; `hal_caps().radio` reflecting the probe; the file-statics `s_radio` and `BEACON_HZ`; and a `radio_listen()` helper Task 4 reuses. **Do not modify `src/hal/hal.h`.**

- [ ] **Step 1: Add the BSP radio includes**

In `src/hal/hal_target.c`, after the existing `#include "sensors/bmi323.h"` line:

```c
#include "radio/cc1101.h"
#include "radio/ook_tx.h"
#include "radio/gdo_capture.h"
#include "platform/ioexp.h"
```

`wilibsp/bsp/CMakeLists.txt` already compiles all of these into the BSP library, so **no CMake change is needed** and `wilibsp/` must not be touched.

- [ ] **Step 2: Add the radio state and frequency**

Next to the existing `static bool s_imu;` declaration, add:

```c
/* 433.92 MHz ISM. A hardware-routing fact, not app policy, so it lives here
   rather than in core/beacon.h beside the wire format. */
#define BEACON_HZ 433920000u
static bool s_radio;
```

- [ ] **Step 3: Add the listen helper**

In `src/hal/hal_target.c`, immediately before the existing `void hal_init(void)`:

```c
/* Put the CC1101 back into async-transparent OOK RX, where GDO0 carries the
   demodulated data edges that gdo_capture timestamps. Both bring-up and the end
   of every transmit return here, so listening is the resting state. */
static void radio_listen(void) {
    cc1101_monitor_rx(BEACON_HZ, CC1101_MOD_ASK_OOK);
}
```

- [ ] **Step 4: Bring the radio up in `hal_init`**

In `hal_init`, immediately after the existing `s_imu = bmi323_init();` line:

```c
    /* Route a CC1101 antenna before any radio SPI traffic. ioexp_antenna talks
       to the PCAL6524 over I2C1 -- the same bus sensor_cb owns -- so it must stay
       here in init, before any timer exists, and never move into a callback. */
    ioexp_antenna(ANT_CC1101_433);
    s_radio = cc1101_init();          /* DIAGs PARTNUM/VERSION itself */
    if (s_radio) {
        gdo_capture_init();
        gdo_capture_start();
        radio_listen();
    }
    DIAG("radio: cc1101 %s\n", s_radio ? "ok" : "ABSENT (beacon disabled)");
```

- [ ] **Step 5: Implement `hal_beacon_tx`**

Replace this line in `src/hal/hal_target.c`:

```c
void hal_beacon_tx(const uint8_t wire[BEACON_WIRE_LEN]) { (void)wire; }                       /* Plan C */
```

with:

```c
/* Blocking: a frame is 136 bits x 2 half-bits x 500 us = ~136 ms of GPIO
   toggling. ook_tx_send drives GDO0 (GPIO32) directly and touches no SPI; only
   the short start/stop register bursts do, and those take the shared bus through
   the BSP's own spi_bus arbiter. The caller gates the cadence. */
void hal_beacon_tx(const uint8_t wire[BEACON_WIRE_LEN]) {
    if (!s_radio) return;
    uint32_t durs[BEACON_MAX_DURS];
    bool start_level = false;
    size_t n = beacon_ook_encode(wire, durs, BEACON_MAX_DURS, &start_level);
    if (n == 0) return;

    cc1101_tx_ook_start(BEACON_HZ);              /* key the carrier; GDO0 becomes SIO */
    ook_tx_send(durs, (uint32_t)n, start_level);
    cc1101_tx_ook_stop();
    gdo_capture_attach_pin();                    /* undo the SIO takeover */
    radio_listen();
}
```

- [ ] **Step 6: Report the capability**

In `hal_caps()`, change `.radio=false` to `.radio=s_radio`, giving:

```c
hal_caps_t hal_caps(void) {
    hal_caps_t c = { .radio=s_radio,.imu=s_imu,.light=s_light,.audio=s_audio_ok,
                     .buttons=true,.leds=true,.dvi=s_dvi };
    return c;
}
```

- [ ] **Step 7: Verify the device builds**

Run: `powershell -File tools/build.ps1 -Clean` (pass `timeout: 600000`)

Expected: zero warnings, no RAM overflow, `wilidoro.uf2` produced. Quote the linker's memory-region usage table in your report. `durs[BEACON_MAX_DURS]` is 276 × 4 = 1104 bytes on the stack, not in bss.

- [ ] **Step 8: Verify the host suite and simulator are untouched**

Run: `powershell -File tools/test.ps1` then `cmake --build build-sim`

Expected: 11/11, and the simulator links (this task changes only `hal_target.c`, which the simulator does not compile).

- [ ] **Step 9: Commit**

```bash
git add src/hal/hal_target.c
git commit -m "$(cat <<'EOF'
feat(hal): bring up the CC1101 and make hal_beacon_tx real

Routes a 433 MHz CC1101 antenna, probes the chip, starts the PIO2/DMA
GDO0 edge capture, and leaves the radio listening -- RX is the resting
state, and every transmit returns to it.

hal_beacon_tx encodes the wire frame to an OOK duration timeline and
bit-bangs it out GDO0. That burst is ~136 ms of blocking GPIO toggling
but touches no SPI at all; only the short start/stop register bursts do,
and the BSP's spi_bus arbiter already owns that handoff including the
GPIO8 LCD_DC/MISO mux. The LVGL flush path is untouched.

ioexp_antenna talks to the PCAL6524 over I2C1, the bus sensor_cb owns, so
it stays in init before any timer exists.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: `hal_beacon_rx` and the boot loopback self-test

**Files:**
- Modify: `src/hal/hal_target.c` (include, framer state, `hal_beacon_rx`, self-test, `hal_init`)

**Interfaces:**
- Consumes: `beacon_rx_t`, `beacon_rx_init`, `beacon_rx_push`, `BEACON_GAP_US` from Task 1; `hal_beacon_tx`, `s_radio`, `BEACON_HZ`, `radio_listen` from Task 3; `beacon_pack`, `beacon_unpack` from `core/beacon.h`; BSP `gdo_capture_drain`.
- Produces: a working `bool hal_beacon_rx(uint8_t wire[BEACON_WIRE_LEN])`. Nothing later consumes anything new.

- [ ] **Step 1: Add the framer include and state**

In `src/hal/hal_target.c`, after the existing `#include "radio/gdo_capture.h"` line:

```c
#include "beacon_rx.h"
```

Next to the `static bool s_radio;` declaration from Task 3:

```c
static beacon_rx_t s_brx;
```

- [ ] **Step 2: Implement `hal_beacon_rx`**

Replace this line in `src/hal/hal_target.c`:

```c
bool hal_beacon_rx(uint8_t wire[BEACON_WIRE_LEN]) { (void)wire; return false; }               /* Plan C */
```

with:

```c
#define RX_DRAIN_CHUNK 128u

/* Drain the PIO2 capture ring fully and feed the framer. The loop matters: during
   a burst the line carries up to ~2000 edges/s, so a single fixed-size drain can
   fall behind and lose the middle of a frame. Draining until a short read means
   the chunk size stops mattering.

   Returning early on a decoded frame leaves the remaining edges in the ring, so
   they are not lost -- only the tail of the current chunk is dropped, and the
   next gap re-arms the framer past it. Do not add a pending-duration queue: a
   frame takes ~136 ms to transmit while this is polled every 100 ms, so two
   complete frames in one chunk is impossible. */
bool hal_beacon_rx(uint8_t wire[BEACON_WIRE_LEN]) {
    if (!s_radio) return false;
    uint32_t durs[RX_DRAIN_CHUNK];
    for (;;) {
        uint32_t n = gdo_capture_drain(durs, RX_DRAIN_CHUNK);
        for (uint32_t i = 0; i < n; i++)
            if (beacon_rx_push(&s_brx, durs[i], wire)) return true;
        if (n < RX_DRAIN_CHUNK) return false;   /* ring is empty */
    }
}
```

- [ ] **Step 3: Add the loopback self-test**

In `src/hal/hal_target.c`, immediately after `hal_beacon_tx`:

```c
/* One-shot self-test, run at bring-up. PIO2 samples the GDO0 pad even while
   ook_tx_send drives it as an SIO output -- wilibsp's hello_cc1101 sends 24
   pulses and drains exactly 24 edges -- so transmitting our own beacon and
   draining the capture exercises the whole chain: pack -> ook_encode -> ook_tx
   timing -> PIO2/DMA capture -> framer -> ook_decode -> unpack.
   Everything except the RF air path and the CC1101's own demodulator, neither of
   which has ever been demonstrated on this hardware by anyone.
   It fails closed: if the pad-sampling assumption does not hold, the decode
   simply fails and the log says so. It cannot produce a false pass. */
static void radio_loopback_selftest(void) {
    if (!s_radio) return;
    beacon_msg_t m;
    memcpy(m.name, "LOOPBACK", BEACON_NAME_LEN);
    m.state = BST_FOCUS; m.minutes_left = 42; m.completed = 7;
    uint8_t sent[BEACON_WIRE_LEN];
    beacon_pack(&m, sent);

    beacon_rx_init(&s_brx);
    hal_beacon_tx(sent);                     /* also returns the radio to listening */

    /* A gap first, so the framer knows the following run is high. */
    uint8_t got[BEACON_WIRE_LEN];
    bool ok = false;
    (void)beacon_rx_push(&s_brx, BEACON_GAP_US * 4u, got);
    for (int round = 0; round < 8 && !ok; round++) {
        uint32_t durs[128];
        uint32_t n = gdo_capture_drain(durs, 128);
        for (uint32_t i = 0; i < n && !ok; i++)
            if (beacon_rx_push(&s_brx, durs[i], got)) ok = true;
        if (!ok) sleep_ms(5);
    }
    if (ok) ok = (memcmp(sent, got, BEACON_WIRE_LEN) == 0);
    DIAG("beacon: loopback %s\n", ok ? "ok" : "FAILED");

    beacon_rx_init(&s_brx);                  /* discard self-test state */
}
```

The framer is re-initialised at the end so no half-consumed self-test segment can be mistaken for the start of a real frame. Any edges still sitting in the capture ring are harmless: the first real gap re-arms the framer past them.

- [ ] **Step 4: Call the self-test from `hal_init`**

In `hal_init`, replace the `radio_listen();` line added in Task 3 with:

```c
        radio_listen();
        radio_loopback_selftest();
```

so the block reads:

```c
    if (s_radio) {
        gdo_capture_init();
        gdo_capture_start();
        radio_listen();
        radio_loopback_selftest();
    }
```

- [ ] **Step 5: Verify the device builds**

Run: `powershell -File tools/build.ps1 -Clean` (pass `timeout: 600000`)

Expected: zero warnings, no RAM overflow. Quote the memory-region usage table. Note `beacon_rx_t` is 276×4 + 16 ≈ 1120 bytes of bss.

- [ ] **Step 6: Verify the host suite and simulator**

Run: `powershell -File tools/test.ps1` then `cmake --build build-sim`

Expected: 11/11 and a clean simulator link.

- [ ] **Step 7: Commit**

```bash
git add src/hal/hal_target.c
git commit -m "$(cat <<'EOF'
feat(hal): decode received beacons, and self-test the chain at boot

hal_beacon_rx drains the PIO2 capture ring into the pure framer. It
returns on the first decoded frame and drops the rest of that drain on
purpose: a frame takes 136 ms to send while this is polled every 100 ms,
so two complete frames in one buffer is impossible.

The boot loopback self-test is the answer to a real gap -- over-the-air
RX has never been demonstrated on this hardware by anyone, and the BSP's
own findings say proving it needs an external transmitter. PIO2 samples
the GDO0 pad even while ook_tx_send drives it as SIO, so transmitting our
own beacon and draining the capture exercises everything except the air
path and the CC1101 demodulator. It fails closed: a wrong assumption
makes the decode fail and log FAILED, never a false pass.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: Transmit cadence, and the hardware notes

**Files:**
- Modify: `src/app/app.c` (transmit deadline, move the RX drain into `sensor_cb`, the TX block in `tick_cb`, `app_init`)
- Modify: `docs/hardware-notes.md` (radio notes + on-device checklist)

No new `#include` is needed in `app.c`: `beacon.h` arrives via `app_model.h`, and
`sound.h` (for `sound_active`) is already included.

**Interfaces:**
- Consumes: `app_beacon_msg` from Task 2; `hal_beacon_tx` from Task 3; `sound_active` (existing, `src/app/sound.h`); `beacon_pack` (existing).
- Produces: nothing new — this is the last task.

- [ ] **Step 1: Add the transmit deadline**

In `src/app/app.c`, next to the existing `static uint32_t s_next_lux;` declaration:

```c
static uint32_t s_next_beacon_ms;   /* next beacon transmit deadline */
```

And with the other cadence constants near `#define FOCUS_TICK_MS   60000u`:

```c
/* Neighbour entries expire after NEIGHBOR_TTL_MS (60 s), so transmitting every
   20 s gives a listener three chances before it drops us. */
#define BEACON_TX_MS 20000u
```

- [ ] **Step 2: Move the beacon receive drain into `sensor_cb`**

Receive currently runs in the 200 ms `tick_cb`, which is too slow: during a burst
the line carries up to ~2000 edges per second, so the capture ring can overrun
between polls and lose the middle of a frame. It moves to the 100 ms `sensor_cb`,
already the home for polling hardware.

In `tick_cb`, **delete** these two lines (leaving `neighbor_expire` where it is):

```c
    uint8_t wire[BEACON_WIRE_LEN];
    if (hal_beacon_rx(wire)) { beacon_msg_t m; if (beacon_unpack(wire, &m)) neighbor_upsert(&s_app.neighbors, &m, now); }
```

Also delete the now-stale comment line directly above them:

```c
    /* beacon rx -> neighbor table (Plan C makes tx/rx real; sim fakes rx) */
```

Then in `sensor_cb`, immediately **before** the existing `/* auto-dim: poll lux at ~2 Hz ... */` comment, add:

```c
    /* Beacon receive lives here rather than in tick_cb because the capture ring
       fills at up to ~2000 edges/s during a burst; at 200 ms it could overrun and
       lose the middle of a frame. Transmit stays on the slower tick -- a 20 s
       period does not need 100 ms granularity. */
    uint8_t wire[BEACON_WIRE_LEN];
    if (hal_beacon_rx(wire)) {
        beacon_msg_t m;
        if (beacon_unpack(wire, &m)) neighbor_upsert(&s_app.neighbors, &m, now);
    }
```

`neighbor_expire` stays in `tick_cb` — it is pure bookkeeping with no hardware
deadline, and 200 ms is ample for a 60 s TTL.

- [ ] **Step 3: Transmit from `tick_cb`**

In `tick_cb`, immediately **after** the existing `neighbor_expire(&s_app.neighbors, now);` line:

```c
    /* Beacon transmit. A frame is ~136 ms of blocking GPIO toggling, so never
       start one while a chime is sounding: the stall would stretch the tone
       audibly, which is the only genuinely bad symptom. The countdown only
       updates once a second, so the visual hitch is near-invisible. */
    if (s_app.settings.beacon_on && (int32_t)(now - s_next_beacon_ms) >= 0) {
        if (!sound_active(&s_app.sound)) {
            beacon_msg_t out;
            app_beacon_msg(&s_app.settings, &s_app.pomo, now, &out);
            uint8_t tx[BEACON_WIRE_LEN];
            beacon_pack(&out, tx);
            hal_beacon_tx(tx);
            s_next_beacon_ms = now + BEACON_TX_MS;
        }
        /* Sound playing: leave the deadline expired and retry on the next tick. */
    }
```

- [ ] **Step 4: Initialise the deadline**

In `app_init`, immediately after the existing `s_next_lux = 0;` line:

```c
    s_next_beacon_ms = 0;
```

- [ ] **Step 5: Verify the simulator builds and links**

Run: `cmake --build build-sim`

Expected: compiles and links, zero warnings. The simulator's `hal_beacon_tx` is a no-op, so behaviour there is unchanged apart from the call.

- [ ] **Step 6: Verify the host suite**

Run: `powershell -File tools/test.ps1`

Expected: `100% tests passed, 0 tests failed out of 11`.

- [ ] **Step 7: Verify the device builds**

Run: `powershell -File tools/build.ps1 -Clean` (pass `timeout: 600000`)

Expected: zero warnings, no RAM overflow, `wilidoro.uf2` produced. Quote the memory-region usage table and compare against the 441,396-of-532,480-byte baseline recorded in the Global Constraints.

- [ ] **Step 8: Document the radio and its checklist**

Append to `docs/hardware-notes.md`, immediately before the final `---` separator and its "Why this note isn't in the BSP" paragraph:

```markdown
## CC1101 focus beacon — cadence and what is unproven

*Not yet hardware-verified. Everything below is reasoned from the BSP's own
driver docs and its 2026-07-04 radio findings; nothing in this section has been
observed on a wilidoro board.*

Wilidoro broadcasts its focus state at **433.92 MHz** (`BEACON_HZ` in
`src/hal/hal_target.c`) as OOK Manchester, and listens for other wilidoros to
populate the Nearby screen. Listening is the resting state; every transmit
returns to it.

| tunable | value | where | meaning |
|---|---|---|---|
| `BEACON_TX_MS` | 20 000 | `src/app/app.c` | transmit period; `NEIGHBOR_TTL_MS` is 60 s, so a listener gets three chances |
| `BEACON_HALFBIT_US` | 500 | `src/core/beacon.h` | OOK half-bit, so a frame is ~136 ms |
| `BEACON_GAP_US` | 2 500 | `src/core/beacon_rx.h` | inter-frame gap threshold; `beacon_ook_decode` rejects runs over 4 half-bits |

**A transmit blocks core 0 for ~136 ms** (136 bits × 2 half-bits × 500 µs) and is
therefore gated on `!sound_active()` — a stall during a chime would stretch the
tone audibly, and that is the only genuinely bad symptom. The countdown updates
once a second, so the visual hitch is near-invisible. Core 1 is free and would
remove the hitch entirely; it was deliberately not used, to avoid making SPI1
concurrent and stacking risk on the unproven PIO coexistence below.

**SPI1 needs no new arbitration.** `gdo_capture` is off-bus by construction,
`ook_tx_send` bit-bangs GDO0 (GPIO32) and touches no SPI, and the CC1101's short
register bursts already go through the BSP's `spi_bus_acquire_cc1101()` (which
also handles the GPIO8 LCD_DC ↔ MISO mux). With a blocking LCD flush and every
caller on an LVGL timer on core 0, SPI1 has exactly one owner at any instant. The
LVGL flush path is deliberately unchanged.

### Two things genuinely unproven

1. **Over-the-air RX has never been demonstrated on this hardware by anyone.**
   `wilibsp`'s `hello_cc1101` phase 3 is a *same-pad plumbing test* — GDO0 is one
   pin, chip-RX-out XOR MCU-TX-in — and the BSP's findings say proving a real
   demod path "needs an external 433 MHz transmitter." Wilidoro's boot loopback
   self-test (`beacon: loopback ok` / `FAILED` over RTT) covers the whole chain
   *except* the air path and the CC1101's demodulator. Do not read a passing
   loopback as working reception.
2. **Three-PIO coexistence is unproven on silicon.** Wilidoro already uses PIO0
   (I2S audio) and PIO1 (WS2812); radio capture is PIO2. The BSP records this as
   "architecturally sound but was not co-exercised on silicon." **If audio or the
   LEDs break once the radio is live, this is the first suspect** — bisect by
   skipping `gdo_capture_init()`/`gdo_capture_start()` in `hal_init`.

### On-device beacon checklist

None of this has been run. **Ask before flashing.**

1. RTT at boot reports `cc1101: PARTNUM=0x00 VERSION=0x14` and `radio: cc1101 ok`.
   A `VERSION` of `0x00` or `0xFF` means nothing answered on SPI1 and the beacon
   stays disabled.
2. RTT reports `beacon: loopback ok`. A `FAILED` here means the chain broke
   somewhere between `beacon_pack` and `beacon_unpack` — and because the
   self-test cannot false-pass, it is worth trusting.
3. **Audio still works with the radio live** — play a start chime and confirm it
   is clean. This is the three-PIO check and the most likely regression.
4. **The LEDs still animate** with the radio live (PIO1 alongside PIO2).
5. With **Beacon** on, the ~136 ms transmit hitch every 20 s is not visibly
   disruptive to the countdown, and never audibly stretches a chime.
6. With **Beacon** off, no transmit happens at all.
7. Auto-dim, touch and the tilt gate all still behave — the transmit stall must
   not break the 100 ms `sensor_cb` cadence beyond a skipped sample.
8. **Conditional on a second transmitter.** A FreeWili One is also on the bench,
   but `wilibsp` only supports `freewili2`, so it cannot run wilidoro firmware.
   If it carries a 433 MHz CC1101 and its own tooling can send raw OOK, the real
   two-device test becomes available: one board transmitting, the other listing it
   on the Nearby screen. Until then the Nearby screen showing nothing on hardware
   is expected and proves nothing either way.
```

- [ ] **Step 9: Commit**

```bash
git add src/app/app.c docs/hardware-notes.md
git commit -m "$(cat <<'EOF'
feat(app): transmit the focus beacon every 20 s

Gated on the Beacon setting and on audio being idle. A frame is ~136 ms
of blocking GPIO toggling, and the audio gate is the whole mitigation:
stalling mid-chime would stretch the tone audibly, which is the only
symptom that actually matters. The countdown updates once a second, so
the visual hitch is near-invisible. When a sound is playing the deadline
is left expired so the next tick retries rather than skipping a period.

Documents the cadence, the reason SPI1 needs no new arbitration, and --
plainly -- the two things nobody has proven: over-the-air RX on this
hardware, and PIO0/PIO1/PIO2 coexisting on silicon. Marks the whole
section unverified rather than implying the design was measured.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Definition of Done

- Host suite: **11 binaries, 11/11 passing**, zero warnings.
- Device build: clean, zero warnings, no RAM overflow.
- Simulator: builds and links; behaviour unchanged (its `hal_beacon_tx` is a no-op and its fake RX still feeds the Nearby screen).
- Task 1 Step 7's mutation check performed and both outcomes reported, with `src/core/beacon_rx.c` restored.
- `src/hal/hal.h`, `src/target/lvgl_port.c` and `wilibsp/` all untouched — confirm with `git diff --stat` against the branch base.
- Hardware verification is **deliberately not** part of this plan's done: it needs the user's explicit go-ahead, and the checklist in `docs/hardware-notes.md` is marked unverified until then.
