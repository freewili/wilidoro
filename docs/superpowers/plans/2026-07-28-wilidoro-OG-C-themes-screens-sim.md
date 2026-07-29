# Wilidoro OG Plan C — Themes, Screens and the Simulator — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish the OG's user interface — the arcade and flip themes at 320×240, the Settings and Nearby screens with their Back softkeys, and an OG geometry mode for the SDL simulator so future layout work needs no board.

**Architecture:** Purely a UI-layer plan. Every change is either a geometry macro added to `src/ui/ui.h`'s existing per-board block, a literal replaced by one of those macros, or the removal of a `#if !defined(WILIDORO_BOARD_OG)` guard that Plan A added as a placeholder. No HAL changes, no new hardware.

**Tech Stack:** C11, LVGL 9.2.2, Pico SDK 2.3.0 (RP2040), SDL2 (simulator), CMake + Ninja, CTest.

**Spec:** `docs/superpowers/specs/2026-07-28-wilidoro-og-port-design.md`
**Predecessors:** Plan OG-A (complete, hardware-verified), Plan OG-B tasks 1–3 (complete; task 4, tilt, still open)

## Global Constraints

- **`src/ui/` and `src/app/` are SHARED with the FreeWili 2.** Every geometry macro's `#else` (FW2) branch **must equal the literal currently at that call site**, so the FreeWili 2 renders byte-identically. This is the single most important property in this plan — a wrong FW2 value silently changes shipping firmware that no OG test would catch.
- **Derive, don't hardcode.** Plan A established `UI_W`, `UI_H`, `UI_FACE_H`, `UI_FONT_BIG`, `UI_SOFTKEY_*` in `src/ui/ui.h`. New geometry goes in the same per-board block, and themes reference the macros. `theme_neon.c` is the pattern to copy (its arc is `UI_FACE_H - 66`).
- **`hal_pump()` must remain the single `fwog_power_poll()` call site, once per main-loop iteration.**
- **Never add a watchdog to the display CPU.** Do not touch `src/main_og/main.c`.
- **Keep the bounded ST7789 init** in `src/target_og/main.c` before `lvgl_port_og_init()`, and keep `ws2812_init()` / `i2s_audio_init()` from Plan OG-B.
- **RAM is the budget to watch.** Plan OG-B left `wilidoro_display` at roughly 152 KB of the RP2040's 264 KB. Two more themes and two more screens are the largest single addition this port makes. `LV_MEM_SIZE` in `config/lv_conf.h` is the knob; read the linker's `-Wl,--print-memory-usage` output every task and stop if RAM crosses ~200 KB.
- **No `printf`** — `DIAG()` only, never with floats or `%f`.
- **`wiliOGbsp/` is a read-only submodule.**
- **The FreeWili 2 build must not regress** and all **12** host test binaries must stay green.
- **Flashing is always `powershell -File tools/flash_og.ps1`.** Never flash a display app by UF2 — the display CPU has no BOOTSEL button.
- **Commit discipline:** every task ends on a green build/test and a commit. Trailer:
  `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`

## The geometry macros this plan adds

All derived from the FreeWili 2 literals currently in the source, so the FW2 branch is unchanged by construction. Add these to the existing per-board block in `src/ui/ui.h`:

| Macro | Derivation | FW2 | OG | Replaces |
|---|---|---|---|---|
| `UI_LIST_W` | `UI_W - 4` | 476 | 316 | `screen_settings.c:86`, `screen_nearby.c:18` |
| `UI_LIST_H` | `UI_FACE_H - 50` | 236 | 162 | same two call sites |
| `UI_ROW_W` | `UI_W - 20` | 460 | 300 | `screen_nearby.c:44` |
| `UI_ROW_H` | per board | 44 | 34 | `screen_settings.c:49`, `screen_nearby.c:44` |
| `UI_STEP_BTN_W` / `_H` | per board | 34 / 30 | 28 / 24 | `screen_settings.c:64,73` |
| `UI_ARCADE_BAR_W` | `UI_W - 80` | 400 | 240 | `theme_arcade.c:48` |
| `UI_ARCADE_BAR_H` | per board | 22 | 16 | `theme_arcade.c:48` |
| `UI_FLIP_CARD` | per board | 150 | 100 | `theme_flip.c:16,25` |
| `UI_FLIP_BAR_W` | `UI_W - 180` | 300 | 140 | `theme_flip.c:59` |

Verify each FW2 column against the source before trusting this table — it was derived by reading, and a transcription slip here is exactly the failure mode that matters.

---

## File Structure

```
src/ui/
  ui.h                 MODIFIED  the macros above, in the existing per-board block
  theme_arcade.c       MODIFIED  Task 1 — geometry macros
  theme_flip.c         MODIFIED  Task 2 — geometry macros
  theme.c              MODIFIED  Task 2 — restore the three-theme registry on OG
  screen_settings.c    MODIFIED  Task 3 — geometry + Back softkey
  screen_nearby.c      MODIFIED  Task 3 — geometry + Back softkey
  screen_timer.c       MODIFIED  Task 3 — restore the Settings/Nearby softkeys on OG
src/app/
  app.c                MODIFIED  Task 3 — remove Plan A's OG guards
src/sim/
  main.c               MODIFIED  Task 4 — geometry from ui.h, no DVI window on OG
  CMakeLists.txt       MODIFIED  Task 4 — WILIDORO_BOARD option
src/hal/
  hal_sim.c            MODIFIED  Task 4 — 7 LEDs and OG capabilities in OG mode
cmake/board_og.cmake   MODIFIED  Tasks 1-3 — link the newly un-deferred sources
tools/sim.ps1          MODIFIED  Task 4 — pass the board through
docs/hardware-notes.md MODIFIED  Tasks 1-3 — what the themes actually look like
```

---

### Task 1: The arcade theme at 320×240

**Files:** Modify `src/ui/ui.h`, `src/ui/theme_arcade.c`, `cmake/board_og.cmake`

**Interfaces:**
- Consumes: `UI_W`, `UI_H`, `UI_FACE_H`, `UI_FONT_BIG` (Plan A).
- Produces: `UI_ARCADE_BAR_W`, `UI_ARCADE_BAR_H` in `ui.h`; `theme_arcade.c` compiled into `wilidoro_display`. Task 2 restores the theme registry that makes it reachable.

- [ ] **Step 1: Add the macros**

In `src/ui/ui.h`, inside the existing `#if defined(WILIDORO_BOARD_OG)` block add `#define UI_ARCADE_BAR_H 16` and in the `#else` branch `#define UI_ARCADE_BAR_H 22`. Below the block, with the other derived values, add:

```c
#define UI_ARCADE_BAR_W   (UI_W - 80)     /* 400 on FW2, 240 on the OG */
```

- [ ] **Step 2: Replace the literals in `theme_arcade.c`**

Two sites, and only two — confirm with `grep -nE "480|, 320|montserrat_48|set_size" src/ui/theme_arcade.c` before and after.

`src/ui/theme_arcade.c:36`:
```c
    lv_obj_set_style_text_font(s_time, UI_FONT_BIG, 0);
```

`src/ui/theme_arcade.c:48`:
```c
    lv_obj_set_size(s_bar, UI_ARCADE_BAR_W, UI_ARCADE_BAR_H);
```

Add `#include "ui.h"` if the file does not already have it. Change nothing else — colours, the mascot, the score layout and every spacing ratio stay as they are.

- [ ] **Step 3: Verify the FreeWili 2 is unchanged**

Run: `powershell -File tools/build.ps1`
Expected: green. `UI_ARCADE_BAR_W` must expand to exactly 400 and `UI_ARCADE_BAR_H` to 22 on that build — the values the file had before. If either differs, the macro is wrong, not the call site.

- [ ] **Step 4: Link it into the OG target**

In `cmake/board_og.cmake`, add `src/ui/theme_arcade.c` to `wilidoro_display`'s sources. It is not yet reachable at runtime — Task 2 restores the registry — but it must compile against the OG's geometry now, which is what this step proves.

Run: `powershell -File tools/build_og.ps1`
Expected: green. **Record the RAM figure** from `-Wl,--print-memory-usage` and compare it against Plan OG-B's ~152 KB.

- [ ] **Step 5: Run the host tests**

Run: `powershell -File tools/test.ps1`
Expected: 12/12. This task changes UI geometry only.

- [ ] **Step 6: Commit**

```bash
git add src/ui/ui.h src/ui/theme_arcade.c cmake/board_og.cmake
git commit -m "feat(og): arcade theme geometry for 320x240

Derives the health bar from UI_W and takes the countdown font from
UI_FONT_BIG, following theme_neon's pattern. The FreeWili 2 branch of each
macro is the literal that was at the call site, so its rendering is
unchanged by construction."
```

---

### Task 2: The flip-clock theme, and the three-theme registry

**Files:** Modify `src/ui/ui.h`, `src/ui/theme_flip.c`, `src/ui/theme.c`, `cmake/board_og.cmake`, `docs/hardware-notes.md`

**Interfaces:**
- Consumes: Task 1's macros and `theme_arcade.c` in the OG target.
- Produces: `UI_FLIP_CARD`, `UI_FLIP_BAR_W`; a `theme_get()` that returns all three themes on the OG. Task 3's Settings screen is what lets a user actually switch between them.

- [ ] **Step 1: Add the macros**

In `src/ui/ui.h`'s per-board block: `#define UI_FLIP_CARD 100` (OG) and `#define UI_FLIP_CARD 150` (FW2). Below the block:

```c
#define UI_FLIP_BAR_W     (UI_W - 180)    /* 300 on FW2, 140 on the OG */
```

- [ ] **Step 2: Replace the literals in `theme_flip.c`**

Five sites. Confirm with grep before and after.

`:16` — the flip card: `lv_obj_set_size(c, UI_FLIP_CARD, UI_FLIP_CARD);`
`:25` — the mechanical seam: `lv_obj_set_size(seam, UI_FLIP_CARD, 3);`
`:42`, `:48`, `:54` — the `MM`, `SS` and colon fonts: `UI_FONT_BIG` in place of `&lv_font_montserrat_48`.
`:59` — the session bar: `lv_obj_set_size(s_bar, UI_FLIP_BAR_W, 8);`

The seam's 3 px height and the bar's 8 px height stay literal — they are hairlines, not proportional geometry, and they read correctly at both sizes.

- [ ] **Step 3: Restore the three-theme registry on the OG**

Plan A replaced `theme_get()` with an OG branch that always returned `THEME_NEON`, because the other two were not linked. They are now. In `src/ui/theme.c`, delete the `#if defined(WILIDORO_BOARD_OG)` branch entirely so both boards use the same three-entry registry, and delete the comment that said the other two arrive in Plan OG-C.

- [ ] **Step 4: Link it and check RAM**

Add `src/ui/theme_flip.c` to `wilidoro_display` in `cmake/board_og.cmake`.

Run: `powershell -File tools/build_og.ps1`
Expected: green. **Record the RAM figure.** This is the task most likely to move it — three themes now build their widget trees. If RAM has crossed ~200 KB, stop and reduce `LV_MEM_SIZE` before continuing.

- [ ] **Step 5: Verify the FreeWili 2 and the host tests**

Run: `powershell -File tools/build.ps1` then `powershell -File tools/test.ps1`
Expected: FW2 green with `UI_FLIP_CARD` = 150 and `UI_FLIP_BAR_W` = 300; 12/12 host tests.

- [ ] **Step 6: Flash and look at all three faces**

Run: `powershell -File tools/flash_og.ps1`

The OG still has no way to *switch* themes until Task 3 adds Settings, so verify what you can: the timer face renders correctly in whichever theme is the default, and nothing regressed.

**Needs a human or a camera.** Record in `docs/hardware-notes.md` what the arcade and flip faces actually look like at 320×240 once they are reachable — and note honestly if that is not until Task 3.

**Read the existing hardware-notes guidance first:** the FreeWili 2 section records that an uncropped board photo is *not* usable evidence about on-screen colour, because the LEDs sit centimetres from the panel and dominate the camera's white balance. Crop to the panel before judging any palette. That lesson was paid for once already.

- [ ] **Step 7: Commit**

```bash
git add src/ui/ui.h src/ui/theme_flip.c src/ui/theme.c cmake/board_og.cmake docs/hardware-notes.md
git commit -m "feat(og): flip-clock theme geometry, and all three themes registered

theme_get()'s OG special case is gone: arcade and flip are linked now, so
both boards share one registry again."
```

---

### Task 3: Settings and Nearby, with Back softkeys

The largest task: it removes every guard Plan A left behind, and gives the OG's five buttons somewhere to go.

**Files:** Modify `src/ui/ui.h`, `src/ui/screen_settings.c`, `src/ui/screen_nearby.c`, `src/ui/screen_timer.c`, `src/app/app.c`, `cmake/board_og.cmake`

**Interfaces:**
- Consumes: everything from Tasks 1–2.
- Produces: `UI_LIST_W/H`, `UI_ROW_W/H`, `UI_STEP_BTN_W/H`; a fully navigable three-screen OG app.

- [ ] **Step 1: Add the macros**

Per-board block: `UI_ROW_H` 34 (OG) / 44 (FW2); `UI_STEP_BTN_W` 28 / 34; `UI_STEP_BTN_H` 24 / 30. Below the block:

```c
#define UI_LIST_W   (UI_W - 4)          /* 476 on FW2, 316 on the OG */
#define UI_LIST_H   (UI_FACE_H - 50)    /* 236 on FW2, 162 on the OG */
#define UI_ROW_W    (UI_W - 20)         /* 460 on FW2, 300 on the OG */
```

- [ ] **Step 2: Replace the literals**

`src/ui/screen_settings.c:49` → `lv_obj_set_size(row, lv_pct(100), UI_ROW_H);`
`:64` → `lv_obj_set_size(minus, UI_STEP_BTN_W, UI_STEP_BTN_H);`
`:73` → `lv_obj_set_size(plus,  UI_STEP_BTN_W, UI_STEP_BTN_H);`
`:86` → `lv_obj_set_size(s_list, UI_LIST_W, UI_LIST_H);`

`src/ui/screen_nearby.c:18` → `lv_obj_set_size(s_list, UI_LIST_W, UI_LIST_H);`
`:44` → `lv_obj_set_size(row, UI_ROW_W, UI_ROW_H);`

Add `#include "ui.h"` to either file if missing.

- [ ] **Step 3: Nothing to do — both screens already leave correctly**

**This step exists to stop you "fixing" something that is not broken.** The design doc originally said these screens would need a Back softkey on the rightmost column, because the FreeWili 2 leaves them via HOME/CANCEL which the OG lacks. That was wrong, and reading the source settles it:

- `screen_settings.c` — `const char *lbl[5] = {"Back", 0, "Default", 0, "Save"};` and `screen_settings_softkey()` routes `col==0` to `app_goto(SCREEN_TIMER)`.
- `screen_nearby.c` — `const char *lbl[5] = {"Back",0,0,0,0};` and `screen_nearby_softkey()` routes `col==0` likewise.

Both already put **Back on column 0, the grey button**, and neither depends on HOME or CANCEL. The five columns map 1:1 onto the OG's five physical buttons, so these screens are already OG-compatible as written.

**Do not add a Back on column 4.** On Settings that column is **Save** — adding Back there would silently destroy the ability to commit a settings change, and the label table would disagree with the handler. Leave both label tables and both handlers exactly as they are.

The design doc has been corrected to match.

- [ ] **Step 4: Restore the timer screen's route into them**

In `src/ui/screen_timer.c`, `labels_for_state()` currently blanks columns 3 and 4 on the OG. Restore `"Nearby"` (column 3) and `"Menu"` (column 4) for the idle, running and paused label sets, so both boards use one table again — delete the `#if defined(WILIDORO_BOARD_OG)` branch.

In `screen_timer_softkey()`, delete every `#if !defined(WILIDORO_BOARD_OG)` guard around the `app_goto(SCREEN_NEARBY)` / `app_goto(SCREEN_SETTINGS)` branches. **All four sites** — the alarm branch, `PM_IDLE`, `PM_PAUSED`, and the running default.

This is what makes those guards safe to remove: Plan A added them because `s_scr[SCREEN_NEARBY]` and `s_scr[SCREEN_SETTINGS]` were never populated on the OG, so a blue or red press would have called `lv_screen_load_anim()` on a null pointer. Step 5 populates them. **Do Step 5 in the same commit** — the tree must never be in a state where the routes exist and the screens do not.

- [ ] **Step 5: Remove Plan A's guards from `app.c`**

Delete all four `#if !defined(WILIDORO_BOARD_OG)` guards in `src/app/app.c`: the `screen_settings.h` / `screen_nearby.h` includes, the two `route_softkey()` switch cases, the two refresh-switch cases, and the two `s_scr[...] = screen_*_create();` lines in `app_init()`. Both boards build the same three screens again.

- [ ] **Step 6: Link the sources and check RAM**

Add `src/ui/screen_settings.c` and `src/ui/screen_nearby.c` to `wilidoro_display` in `cmake/board_og.cmake`.

Run: `powershell -File tools/build_og.ps1`
Expected: green. **Record the RAM figure.** Three screens plus three themes is this port's peak; if it has crossed ~200 KB, reduce `LV_MEM_SIZE` now.

- [ ] **Step 7: Verify the FreeWili 2 and the host tests**

Run: `powershell -File tools/build.ps1` then `powershell -File tools/test.ps1`
Expected: FW2 green and — importantly — **unchanged in behaviour**: every guard removed in Steps 4–5 was `#if !defined(WILIDORO_BOARD_OG)`, which was already true for FW2 builds, so removing them is a no-op there. 12/12 host tests.

- [ ] **Step 8: Flash and navigate the whole app**

Run: `powershell -File tools/flash_og.ps1`

**Needs a human at the board.** Walk the full loop:
- Timer face idle: five labelled softkeys, with Nearby on blue and Menu on red.
- Press red (short) → Settings opens. The rows are readable at 320×240 and the `+`/`−` buttons are hittable.
- Change the theme in Settings and confirm the timer face **rebuilds live** in the new theme, for all three.
- Press red again → Back returns to the timer face.
- Press blue → Nearby opens; it will be empty (the beacon is Plan OG-D). Back works.
- Confirm a **6 s red hold still powers the board off** from each screen — the power path is the BSP's and must not have been shadowed by the new Back binding.
- Watch for clipped softkey labels. `UI_SOFTKEY_BTN_W` is 60 px at a 16 px font, and "Dismiss" is about that wide — the design doc flagged this as unverified, and this is the task that settles it.

- [ ] **Step 9: Record what the screens look like**

Update `docs/hardware-notes.md` with the OG's three faces and both screens, cropped-to-panel, and the softkey label verdict. If any label clips, record the fix rather than only the symptom.

- [ ] **Step 10: Commit**

```bash
git add src/ui/ui.h src/ui/screen_settings.c src/ui/screen_nearby.c \
        src/ui/screen_timer.c src/app/app.c cmake/board_og.cmake docs/hardware-notes.md
git commit -m "feat(og): Settings and Nearby, with Back on the red softkey

Removes every OG guard Plan A left behind. The FreeWili 2's HOME/CANCEL
have no counterpart here, so each screen spends its rightmost column on
Back; a short red press is an ordinary softkey and the BSP's ~6 s hold
still owns power-off."
```

---

### Task 4: An OG geometry mode for the simulator

Pays for itself the first time a theme needs laying out without a board.

**Files:** Modify `src/sim/main.c`, `src/sim/CMakeLists.txt`, `src/hal/hal_sim.c`, `tools/sim.ps1`

**Interfaces:**
- Consumes: `UI_W`, `UI_H` from `ui.h`.
- Produces: `tools/sim.ps1 -Board og` running the OG layout on the desktop.

- [ ] **Step 1: Take the window geometry from `ui.h`**

`src/sim/main.c` hardcodes `lv_sdl_window_create(480, 320)`. Add `#include "ui.h"` and use the macros:

```c
    lv_sdl_window_create(UI_W, UI_H);
```

- [ ] **Step 2: Skip the DVI mirror window on the OG**

The OG has no HSTX and `dvi_view.c` is not in its build. In `src/sim/main.c`, guard both calls:

```c
#if !defined(WILIDORO_BOARD_OG)
    sim_dvi_create();
#endif
```
and the same around `sim_dvi_present();` in the loop.

- [ ] **Step 3: Match the OG's capabilities in `hal_sim.c`**

A simulator that reports capabilities the board does not have is worse than no simulator. Under `#if defined(WILIDORO_BOARD_OG)`:
- `hal_led_count()` returns `7` instead of `16`.
- `hal_lux()` returns `false` (no ambient sensor on the OG).
- `hal_dvi_surface()` returns `false`.
- `hal_caps()` matches `hal_og.c`'s profile.

Leave the FW2 paths exactly as they are.

- [ ] **Step 4: Add the board option to the sim build**

`src/sim/CMakeLists.txt` is a **separate CMake project** from the root — it does not inherit `WILIDORO_BOARD`. Give it its own option, defaulting to `fw2`:

```cmake
set(WILIDORO_BOARD "fw2" CACHE STRING "Simulated board: fw2 or og")
if(WILIDORO_BOARD STREQUAL "og")
    add_compile_definitions(WILIDORO_BOARD_OG=1)
endif()
```

On `og`, drop `dvi_view.c` from the source list — it is FW2-only, exactly as in `cmake/board_og.cmake`.

- [ ] **Step 5: Pass the board through `tools/sim.ps1`**

Add a `-Board` parameter defaulting to `fw2`, forwarded as `-DWILIDORO_BOARD=<value>`, and use a **separate binary directory per board** (`build-sim` and `build-sim-og`) — one CMake cache cannot hold both, the same constraint the device builds have.

- [ ] **Step 6: Run both simulators**

Run: `powershell -File tools/sim.ps1`
Expected: the FreeWili 2 simulator, 480×320, with its DVI mirror window, exactly as before this task.

Run: `powershell -File tools/sim.ps1 -Board og`
Expected: a 320×240 window, no DVI mirror, the neon timer face laid out as on the OG. Keyboard controls are unchanged: `Z X C V B` are the five softkeys — which on the OG are the five physical buttons.

Cycle all three themes through Settings in the OG simulator and compare against the hardware photographs from Task 3. This is the check that the simulator is telling the truth.

- [ ] **Step 7: Run the host tests and both firmware builds**

Run: `powershell -File tools/test.ps1`, `powershell -File tools/build.ps1`, `powershell -File tools/build_og.ps1`
Expected: 12/12 and both firmwares green.

- [ ] **Step 8: Update the README**

The README documents the simulator for the FreeWili 2 only. Add the OG invocation and note what it does *not* simulate — no DVI mirror, 7 LEDs, and the capability profile matching the real board.

- [ ] **Step 9: Commit**

```bash
git add src/sim/main.c src/sim/CMakeLists.txt src/hal/hal_sim.c tools/sim.ps1 README.md
git commit -m "feat(og): OG geometry mode for the SDL simulator

One board per build directory, as on device -- the sim is a separate CMake
project and does not inherit the root's WILIDORO_BOARD. hal_sim reports the
OG's real capability profile in OG mode, because a simulator that claims
hardware the board lacks is worse than none."
```

---

## What Plan OG-C leaves for Plan OG-D

The sub-GHz beacon: link opcodes `0x40`/`0x41` and their host tests, the main-CPU CC1101 application, and the two-radio over-the-air self-test. The Nearby screen this plan restores will stay empty until then — which is honest, not broken, and worth a line in the hardware notes so nobody reports it as a bug.

## Verification that needs a human

| Task | Needs | Why |
|---|---|---|
| 1, 2 | eyes or a cropped camera frame | theme layout and palette |
| 3 | **hands** | navigating Settings and Nearby needs button presses; the softkey-clipping question can only be settled by looking |
| 4 | eyes | comparing the simulator against the hardware photographs |

None of it needs the board to be *tilted* — that is Plan OG-B Task 4, still open.

## Self-review notes

- Spec coverage: the remaining two themes at 320×240; Settings and Nearby with the Back softkey the design specified; the simulator's OG geometry mode. All three were named in the spec's decisions and are implemented here.
- Carried forward from the spec's Plan A outcomes: the softkey-label-fit question (Task 3 Step 8) and the "does the OG surface `hal_caps()` at all" question — this plan restores `screen_settings.c`, which is `hal_caps()`'s only reader, so the OG gains a capability surface for the first time. That resolves the open item the design doc flagged.
- Not addressed here, deliberately: hardening `tools/flash_og.ps1` against a resetting main CPU (tooling, tracked in the design doc), and Plan OG-B Task 4 (tilt).
