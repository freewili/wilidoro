# Wilidoro Plan B2 — Themes (Neon Arc / Tomato Arcade / Flip Clock) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the single neutral timer face into three switchable visual themes — **Neon Arc**, **Tomato Arcade**, **Flip Clock** — behind a small `theme_t` interface, and make the Settings "Theme" row change the face live.

**Architecture:** Extract the pomodoro-state→display mapping into a pure, host-tested `timer_view` module (which also centralizes the idle-preview and paused-break logic that had bugs in B1). Refactor `screen_timer` into a theme-agnostic shell (background + a face container + the softkey bar) that delegates the face to the active `theme_t`. Each theme is a self-contained `.c` implementing `build(parent)` + `update(view)` using only confirmed LVGL 9.2.2 widgets (arc, label, bar, styled objects). A `theme_get(idx)` registry returns one of the three; the Settings theme row cycles it and rebuilds the face.

**Tech Stack:** C11, LVGL 9.2.2 (arc/label/bar/flex/styles/gradients — all confirmed enabled), wilibsp, SDL2 sim, CTest.

## Global Constraints

Every task's requirements implicitly include these.

- **Theme scope is VISUAL only.** A theme controls the timer face widgets + a shared accent/cool color. LED patterns and sounds remain Plan C (the `hal_tone`/LED calls in the controller are untouched here). Do NOT add audio/LED behavior in B2.
- **The softkey bar stays theme-independent.** Its five cell colors map to the physical buttons (grey/yellow/green/blue/red) and must not change per theme. Only the face container and the screen background are re-skinned.
- **Themes never touch `pomodoro_t` internals directly.** They consume a `timer_view_t` (computed once per tick by `timer_view_make`). Themes include `theme.h` + `lvgl.h` only — never `pomodoro.h`, `hal.h`, `app.h`, or wilibsp/SDL.
- **LVGL 9.2.2 specifics:** `LV_USE_FLOAT 0` → arc angles are integer degrees. Fonts: montserrat 14/16/24/48 only. Colors via `lv_color_hex()`. `lv_bar_set_value(obj, v, LV_ANIM_OFF)`. Gradients via `lv_obj_set_style_bg_grad_color/_dir`. Use v9 API names.
- **One active face at a time.** Each theme keeps its widget handles in file-static storage; `screen_timer` calls `lv_obj_clean(face)` before `theme->build(face)` on every (re)build, so stale handles are never read (update is never called between clean and build).
- **Theme indices:** 0 = Neon Arc, 1 = Tomato Arcade, 2 = Flip Clock (matches `app_settings_cycle_theme` 0→1→2→0 and the Settings theme-name table `{"Neon Arc","Arcade","Flip Clock"}`).
- **Hardware access rule:** building firmware, host tests, and the SDL sim run freely. Flashing/RTT/debug-probe/camera steps must NOT run without asking the user first.
- **Commit trailer (every commit):** `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`

## Verification model

- `timer_view` → host CTest (autonomous).
- Themes + refactor → build gates (device `wilidoro.uf2` links no-overflow; sim `wilidoro_sim.exe` links) + sim launch smoke. Visual confirmation of each theme is a **user-gated checkpoint** (sim window or on-device) — batch it, don't assume it.

---

## File Structure

```
src/app/
  timer_view.h/.c    NEW   PURE host-tested: pomodoro_t + alarm + now -> timer_view_t (idle preview, paused-break, session idx)
src/ui/
  theme.h            NEW   theme_t interface + timer_view_t include + theme_get(idx)
  theme.c            NEW   the theme_get() registry (returns &neon/&arcade/&flip)
  theme_neon.c       NEW   Neon Arc face (extracted from B1's screen_timer face)
  theme_arcade.c     NEW   Tomato Arcade face
  theme_flip.c       NEW   Flip Clock face
  screen_timer.c     MOD   becomes a theme-agnostic shell; delegates face to active theme; + screen_timer_apply_theme()
  screen_timer.h     MOD   add `void screen_timer_apply_theme(void);`
  screen_settings.c  MOD   theme row calls screen_timer_apply_theme() after cycling; titles use active theme accent
  ui.h               MOD   add UI_ helpers if needed (accent getter) — minimal
tests/
  test_timer_view.c  NEW   idle preview, paused-break effective state, session idx, focus/break totals
  CMakeLists.txt     MOD   add test_timer_view (compiles src/app/timer_view.c + pomodoro.c)
CMakeLists.txt       MOD   add theme*.c + timer_view.c to wilidoro target
src/sim/CMakeLists.txt MOD add theme*.c + timer_view.c to wilidoro_sim target
```

---

### Task 1: `timer_view` pure module (TDD)

**Files:**
- Create: `src/app/timer_view.h`, `src/app/timer_view.c`, `tests/test_timer_view.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `core/pomodoro.h` (`pomodoro_t`, `pm_state_t`, `pomodoro_remaining_ms`).
- Produces (verbatim header):
```c
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
```
Semantics (the tests encode these): `eff_state = (state==PM_PAUSED) ? resume_state : state`. `idle=(state==PM_IDLE)`, `paused=(state==PM_PAUSED)`, `alarm=alarm_active`. `break_phase = eff_state in {PM_BREAK_SHORT,PM_BREAK_LONG}`. `total_ms` from `eff_state` (focus_min default, short_min if BREAK_SHORT, long_min if BREAK_LONG). `rem_ms = pomodoro_remaining_ms(p, now)`. If `idle && !alarm`: `total_ms = rem_ms = focus_min*60000` (full-ring preview). `session_n = long_every`; `session_idx = long_every ? completed % long_every : 0`. `completed`/`streak` copied from `p->stats`.

- [ ] **Step 1: Write the failing tests**

`tests/test_timer_view.c`:
```c
#include "greatest.h"
#include "timer_view.h"

static pm_config_t CFG = { .focus_min=25, .short_min=5, .long_min=15, .long_every=4 };
#define MIN(n) ((uint32_t)(n)*60000u)

TEST idle_previews_focus_full_ring(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    timer_view_t v = timer_view_make(&p, false, 0);
    ASSERT(v.idle);
    ASSERT_EQ(MIN(25), v.total_ms);
    ASSERT_EQ(MIN(25), v.rem_ms);       /* full ring */
    ASSERT_FALSE(v.break_phase);
    PASS();
}

TEST focus_uses_focus_total(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    pomodoro_start_focus(&p, 0);
    timer_view_t v = timer_view_make(&p, false, MIN(10));
    ASSERT_EQ(PM_FOCUS, v.eff_state);
    ASSERT_EQ(MIN(25), v.total_ms);
    ASSERT_EQ(MIN(15), v.rem_ms);
    ASSERT_FALSE(v.break_phase);
    PASS();
}

TEST paused_break_uses_break_total_and_flag(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    pomodoro_start_focus(&p, 0);
    pomodoro_tick(&p, MIN(25));                 /* -> ALARM */
    pomodoro_acknowledge(&p, MIN(25));          /* -> BREAK_SHORT */
    pomodoro_pause(&p, MIN(25)+MIN(2));         /* pause the break */
    timer_view_t v = timer_view_make(&p, false, MIN(999));
    ASSERT(v.paused);
    ASSERT_EQ(PM_BREAK_SHORT, v.eff_state);     /* effective state, not PAUSED */
    ASSERT_EQ(MIN(5), v.total_ms);              /* break total, not focus */
    ASSERT(v.break_phase);                      /* cool color */
    ASSERT_EQ(MIN(3), v.rem_ms);                /* 5 - 2 paused */
    PASS();
}

TEST session_index_wraps_by_long_every(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    p.stats.completed = 6;                      /* 6 % 4 == 2 */
    timer_view_t v = timer_view_make(&p, false, 0);
    ASSERT_EQ(2u, v.session_idx);
    ASSERT_EQ(4u, v.session_n);
    PASS();
}

TEST alarm_flag_set(void) {
    pomodoro_t p; pomodoro_init(&p, CFG);
    pomodoro_start_focus(&p, 0);
    pomodoro_tick(&p, MIN(25));                 /* ALARM */
    timer_view_t v = timer_view_make(&p, true, MIN(25));
    ASSERT(v.alarm);
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(idle_previews_focus_full_ring);
    RUN_TEST(focus_uses_focus_total);
    RUN_TEST(paused_break_uses_break_total_and_flag);
    RUN_TEST(session_index_wraps_by_long_every);
    RUN_TEST(alarm_flag_set);
    GREATEST_MAIN_END();
}
```
Add to `tests/CMakeLists.txt`:
```cmake
add_executable(test_timer_view test_timer_view.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/timer_view.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/pomodoro.c)
target_include_directories(test_timer_view PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CORE} ${CMAKE_CURRENT_SOURCE_DIR}/../src/app)
target_compile_options(test_timer_view PRIVATE -Wall -Wextra)
target_link_libraries(test_timer_view m)
add_test(NAME test_timer_view COMMAND test_timer_view)
```

- [ ] **Step 2: Run — verify build failure**

Run: `powershell -File tools/test.ps1`
Expected: FAILS (`timer_view.h` not found).

- [ ] **Step 3: Write `timer_view.h` (verbatim above) and `timer_view.c`**

`src/app/timer_view.c`:
```c
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
```

- [ ] **Step 4: Run — verify green (5 cases, suite now 7 binaries)**

Run: `powershell -File tools/test.ps1`
Expected: `test_timer_view` passes all 5; ctest 100%.

- [ ] **Step 5: Commit**

```bash
git add src/app/timer_view.h src/app/timer_view.c tests/test_timer_view.c tests/CMakeLists.txt
git commit -m "feat(app): timer_view state->display mapping with tests"
```

---

### Task 2: `theme_t` interface + Neon theme + screen_timer refactor + Settings wiring

**Files:**
- Create: `src/ui/theme.h`, `src/ui/theme.c`, `src/ui/theme_neon.c`
- Modify: `src/ui/screen_timer.h`, `src/ui/screen_timer.c`, `src/ui/screen_settings.c`, `CMakeLists.txt`, `src/sim/CMakeLists.txt`
- (Temporary) the registry points arcade+flip at the neon theme until Tasks 3–4 replace them.

**Interfaces:**
- Consumes: `timer_view.h` (Task 1), `ui.h`, `lvgl.h`.
- Produces (verbatim `theme.h`):
```c
// src/ui/theme.h
#ifndef WILIDORO_THEME_H
#define WILIDORO_THEME_H
#include "lvgl.h"
#include "timer_view.h"

typedef struct {
    const char *name;
    uint32_t    bg;       /* screen background */
    uint32_t    accent;   /* focus / primary accent (also used by Settings/Nearby titles) */
    uint32_t    cool;     /* break color */
    void (*build)(lv_obj_t *face);     /* create widgets into the face container */
    void (*update)(const timer_view_t *v);  /* refresh from state (build() ran first) */
} theme_t;

const theme_t *theme_get(uint8_t idx);   /* idx 0..2 -> neon/arcade/flip; clamps out-of-range to 0 */

/* Each theme .c exposes its descriptor for the registry: */
extern const theme_t THEME_NEON;
extern const theme_t THEME_ARCADE;
extern const theme_t THEME_FLIP;
#endif
```
- Produces `screen_timer_apply_theme(void)` (declared in `screen_timer.h`) that re-reads `app()->settings.theme`, sets the shell bg from the theme, `lv_obj_clean`s the face, and calls `theme->build(face)`.

- [ ] **Step 1: Write `theme.c` (registry) and `theme_neon.c`**

`src/ui/theme.c`:
```c
#include "theme.h"
const theme_t *theme_get(uint8_t idx) {
    switch (idx) {
        case 1: return &THEME_ARCADE;
        case 2: return &THEME_FLIP;
        default: return &THEME_NEON;
    }
}
```
`src/ui/theme_neon.c` (the B1 neutral face, moved behind the interface):
```c
#include "theme.h"
#include <stdio.h>

#define NEON_BG     0x0C0C12
#define NEON_PANEL  0x141b26
#define NEON_TEXT   0xDDE6F2
#define NEON_MUTED  0x6B7C93
#define NEON_ACCENT 0xFF5B45
#define NEON_COOL   0x2DD4BF

static lv_obj_t *s_arc, *s_time, *s_state;

static void neon_build(lv_obj_t *face) {
    s_arc = lv_arc_create(face);
    lv_obj_set_size(s_arc, 220, 220);
    lv_obj_align(s_arc, LV_ALIGN_CENTER, 0, -6);
    lv_arc_set_rotation(s_arc, 270);
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_arc_set_range(s_arc, 0, 1000);
    lv_arc_set_value(s_arc, 1000);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_arc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(NEON_PANEL), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(NEON_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_INDICATOR);

    s_time = lv_label_create(face);
    lv_obj_set_style_text_color(s_time, lv_color_hex(NEON_TEXT), 0);
    lv_obj_set_style_text_font(s_time, &lv_font_montserrat_48, 0);
    lv_obj_align(s_time, LV_ALIGN_CENTER, 0, -16);
    lv_label_set_text(s_time, "25:00");

    s_state = lv_label_create(face);
    lv_obj_set_style_text_color(s_state, lv_color_hex(NEON_MUTED), 0);
    lv_obj_set_style_text_font(s_state, &lv_font_montserrat_16, 0);
    lv_obj_align(s_state, LV_ALIGN_CENTER, 0, 26);
    lv_label_set_text(s_state, "READY");
}

static void neon_update(const timer_view_t *v) {
    char buf[8]; unsigned s = v->rem_ms/1000;
    snprintf(buf, sizeof buf, "%02u:%02u", s/60, s%60);
    lv_label_set_text(s_time, buf);

    int32_t val = v->total_ms ? (int32_t)((uint64_t)v->rem_ms*1000/v->total_ms) : 0;
    lv_arc_set_value(s_arc, val);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(v->break_phase?NEON_COOL:NEON_ACCENT), LV_PART_INDICATOR);

    const char *name = v->alarm ? "TIME'S UP" :
        v->paused ? "PAUSED" :
        v->idle ? "READY" :
        (v->break_phase ? "BREAK" : "FOCUS");
    char st[24]; snprintf(st, sizeof st, "%s  %u/%u", name, v->session_idx, v->session_n);
    lv_label_set_text(s_state, st);
}

const theme_t THEME_NEON = {
    .name="Neon Arc", .bg=NEON_BG, .accent=NEON_ACCENT, .cool=NEON_COOL,
    .build=neon_build, .update=neon_update,
};
```

- [ ] **Step 2: Refactor `screen_timer.h` + `screen_timer.c` into a themed shell**

`src/ui/screen_timer.h` — add the apply declaration:
```c
#ifndef WILIDORO_SCREEN_TIMER_H
#define WILIDORO_SCREEN_TIMER_H
#include "lvgl.h"
lv_obj_t *screen_timer_create(void);
void      screen_timer_update(void);
void      screen_timer_softkey(int col);
void      screen_timer_apply_theme(void);   /* rebuild the face from app()->settings.theme */
#endif
```
`src/ui/screen_timer.c` — the shell (owns bg + face container + softkey bar; delegates the face; keeps ALL the softkey action logic from B1 unchanged):
```c
#include "screen_timer.h"
#include "ui.h"
#include "app.h"
#include "hal.h"
#include "theme.h"
#include "timer_view.h"
#include "pomodoro.h"

static lv_obj_t *s_scr, *s_face, *s_bar;
static const theme_t *s_theme;

static void on_softkey(int col) { screen_timer_softkey(col); }

void screen_timer_apply_theme(void) {
    s_theme = theme_get(app()->settings.theme);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(s_theme->bg), 0);
    lv_obj_clean(s_face);
    s_theme->build(s_face);
}

lv_obj_t *screen_timer_create(void) {
    s_scr = ui_screen();
    s_face = lv_obj_create(s_scr);
    lv_obj_set_size(s_face, 480, 286);
    lv_obj_align(s_face, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_face, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_face, 0, 0);
    lv_obj_set_style_pad_all(s_face, 0, 0);
    lv_obj_remove_flag(s_face, LV_OBJ_FLAG_SCROLLABLE);

    s_bar = ui_softkey_bar(s_scr, on_softkey);
    screen_timer_apply_theme();     /* builds the initial face + bg */
    screen_timer_update();
    return s_scr;
}

static void labels_for_state(const timer_view_t *v, const char *out[5]) {
    static const char *idle[5]  = {"Start", 0, 0, "Nearby", "Menu"};
    static const char *run[5]   = {"Pause", "Skip", "+5", "Nearby", "Menu"};
    static const char *paused[5]= {"Resume","Skip", "+5", "Nearby", "Menu"};
    static const char *alarm_l[5]={"Dismiss",0,0,0,"Menu"};
    const char **src = idle;
    if (v->alarm) src = alarm_l;
    else if (v->paused) src = paused;
    else if (!v->idle) src = run;
    for (int i=0;i<5;i++) out[i]=src[i];
}

void screen_timer_update(void) {
    timer_view_t v = timer_view_make(&app()->pomo, app()->alarm_active, hal_now_ms());
    s_theme->update(&v);
    const char *lbl[5]; labels_for_state(&v, lbl);
    ui_softkey_set_labels(s_bar, lbl);
}

void screen_timer_softkey(int col) {
    app_t *a = app();
    uint32_t now = hal_now_ms();
    if (a->alarm_active) {
        if (col==0) { pomodoro_acknowledge(&a->pomo, now); a->alarm_active=false; }
        else if (col==4) app_goto(SCREEN_SETTINGS);
        return;
    }
    switch (a->pomo.state) {
        case PM_IDLE:
            if (col==0) pomodoro_start_focus(&a->pomo, now);
            else if (col==3) app_goto(SCREEN_NEARBY);
            else if (col==4) app_goto(SCREEN_SETTINGS);
            break;
        case PM_PAUSED:
            if (col==0) pomodoro_resume(&a->pomo, now);
            else if (col==1) pomodoro_skip(&a->pomo, now);
            else if (col==2) pomodoro_add5(&a->pomo, now);
            else if (col==3) app_goto(SCREEN_NEARBY);
            else if (col==4) app_goto(SCREEN_SETTINGS);
            break;
        default:
            if (col==0) pomodoro_pause(&a->pomo, now);
            else if (col==1) pomodoro_skip(&a->pomo, now);
            else if (col==2) pomodoro_add5(&a->pomo, now);
            else if (col==3) app_goto(SCREEN_NEARBY);
            else if (col==4) app_goto(SCREEN_SETTINGS);
            break;
    }
}
```

- [ ] **Step 3: Wire the Settings theme row to rebuild the face**

In `src/ui/screen_settings.c`, the `adj_event` handler's `SET_THEME` case currently calls `app_settings_cycle_theme(s)`. Add a call to rebuild the timer face right after, and include the header. At the top add `#include "screen_timer.h"`. Change the SET_THEME case to:
```c
        case SET_THEME: app_settings_cycle_theme(s); screen_timer_apply_theme(); break;
```
(The timer face container exists from startup even while Settings is shown, so rebuilding it off-screen is safe; the new theme is visible when the user returns to the timer.)

- [ ] **Step 4: Add sources to both CMakeLists**

Root `CMakeLists.txt` — add to `add_executable(wilidoro ...)`:
```cmake
    src/app/timer_view.c
    src/ui/theme.c
    src/ui/theme_neon.c
    src/ui/theme_arcade.c
    src/ui/theme_flip.c
```
Sim `src/sim/CMakeLists.txt` — add the same files with the `${CMAKE_CURRENT_SOURCE_DIR}/../` prefix (e.g. `${CMAKE_CURRENT_SOURCE_DIR}/../app/timer_view.c`, `.../ui/theme.c`, etc.).
**Because the CMakeLists now reference `theme_arcade.c` and `theme_flip.c` which don't exist until Tasks 3–4, create minimal placeholder files now** so both targets build in this task: `src/ui/theme_arcade.c` and `src/ui/theme_flip.c` each containing a descriptor that ALIASES neon's behavior:
```c
/* src/ui/theme_arcade.c — placeholder until Task 3 */
#include "theme.h"
static void a_build(lv_obj_t *f){ THEME_NEON.build(f); }
static void a_update(const timer_view_t *v){ THEME_NEON.update(v); }
const theme_t THEME_ARCADE = { .name="Arcade", .bg=0x2d1b4e, .accent=0xff4757, .cool=0x2ed573, .build=a_build, .update=a_update };
```
```c
/* src/ui/theme_flip.c — placeholder until Task 4 */
#include "theme.h"
static void f_build(lv_obj_t *f){ THEME_NEON.build(f); }
static void f_update(const timer_view_t *v){ THEME_NEON.update(v); }
const theme_t THEME_FLIP = { .name="Flip Clock", .bg=0x221f1d, .accent=0xC8503C, .cool=0x8a7d6b, .build=f_build, .update=f_update };
```

- [ ] **Step 5: Build both targets + host tests**

Run `powershell -File tools/test.ps1` (7/7 incl. test_timer_view). Then device build (`tools/build.ps1 -Clean`) → links, no overflow. Then sim build (configure + `cmake --build build-sim`) → links. Paste all three results + artifact listings. The Neon face must look identical to B1; cycling the theme in Settings rebuilds the face (arcade shows a purple bg + neon widgets, flip a warm bg + neon widgets — placeholders — proving the switch works).

- [ ] **Step 6: Commit**

```bash
git add src/app/timer_view.* src/ui/theme.h src/ui/theme.c src/ui/theme_neon.c src/ui/theme_arcade.c src/ui/theme_flip.c src/ui/screen_timer.h src/ui/screen_timer.c src/ui/screen_settings.c CMakeLists.txt src/sim/CMakeLists.txt
git commit -m "feat(ui): theme_t interface + Neon theme + themed timer shell + switching"
```

---

### Task 3: Tomato Arcade theme

**Files:**
- Modify: `src/ui/theme_arcade.c` (replace the placeholder with the real face)

**Interfaces:**
- Consumes: `theme.h`, `timer_view.h`. Produces the real `THEME_ARCADE`.

Design: purple bg (`0x2d1b4e`), a pixel-tomato mascot built from styled objects (rounded red body + green stem rects + two dark eyes), a **health-bar** progress (`lv_bar`, red fill on a dark track, colored by phase), the big MM:SS in yellow (`0xffe066`), and a score line "LVL {completed/long_every+1} · x{streak}" in green. No canvas/animation needed.

- [ ] **Step 1: Replace `src/ui/theme_arcade.c`**

```c
#include "theme.h"
#include <stdio.h>

#define AR_BG     0x2d1b4e
#define AR_PANEL  0x1a0f33
#define AR_RED    0xff4757
#define AR_GREEN  0x2ed573
#define AR_YELLOW 0xffe066
#define AR_PINK   0xff6b81

static lv_obj_t *s_time, *s_status, *s_bar, *s_score, *s_body;

static lv_obj_t *rect(lv_obj_t *par, int w,int h,int x,int y,uint32_t col,int radius){
    lv_obj_t *o = lv_obj_create(par);
    lv_obj_set_size(o,w,h); lv_obj_align(o,LV_ALIGN_TOP_LEFT,x,y);
    lv_obj_set_style_bg_color(o,lv_color_hex(col),0);
    lv_obj_set_style_border_width(o,0,0);
    lv_obj_set_style_radius(o,radius,0);
    lv_obj_set_style_pad_all(o,0,0);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void ar_build(lv_obj_t *face) {
    /* mascot: body + stem + eyes, top-left area */
    s_body = rect(face, 96, 80, 40, 60, AR_RED, 26);
    rect(face, 12, 20, 62, 46, AR_GREEN, 3);
    rect(face, 12, 26, 82, 40, AR_GREEN, 3);
    rect(face, 12, 20, 102, 46, AR_GREEN, 3);
    rect(face, 12, 12, 66, 92, AR_BG, 3);   /* left eye */
    rect(face, 12, 12, 100, 92, AR_BG, 3);  /* right eye */

    /* big time, right of mascot */
    s_time = lv_label_create(face);
    lv_obj_set_style_text_color(s_time, lv_color_hex(AR_YELLOW), 0);
    lv_obj_set_style_text_font(s_time, &lv_font_montserrat_48, 0);
    lv_obj_align(s_time, LV_ALIGN_TOP_RIGHT, -30, 66);
    lv_label_set_text(s_time, "25:00");

    s_status = lv_label_create(face);
    lv_obj_set_style_text_color(s_status, lv_color_hex(AR_PINK), 0);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_16, 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_RIGHT, -30, 130);
    lv_label_set_text(s_status, "** READY **");

    /* health-bar progress */
    s_bar = lv_bar_create(face);
    lv_obj_set_size(s_bar, 400, 22);
    lv_obj_align(s_bar, LV_ALIGN_CENTER, 0, 40);
    lv_bar_set_range(s_bar, 0, 1000);
    lv_bar_set_value(s_bar, 1000, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(AR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_bar, lv_color_hex(AR_YELLOW), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(AR_RED), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, 0, LV_PART_INDICATOR);

    s_score = lv_label_create(face);
    lv_obj_set_style_text_color(s_score, lv_color_hex(AR_GREEN), 0);
    lv_obj_set_style_text_font(s_score, &lv_font_montserrat_16, 0);
    lv_obj_align(s_score, LV_ALIGN_CENTER, 0, 78);
    lv_label_set_text(s_score, "LVL 1  x0");
}

static void ar_update(const timer_view_t *v) {
    char buf[8]; unsigned s = v->rem_ms/1000;
    snprintf(buf, sizeof buf, "%02u:%02u", s/60, s%60);
    lv_label_set_text(s_time, buf);

    int32_t val = v->total_ms ? (int32_t)((uint64_t)v->rem_ms*1000/v->total_ms) : 0;
    lv_bar_set_value(s_bar, val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(v->break_phase?AR_GREEN:AR_RED), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_body, lv_color_hex(v->break_phase?AR_GREEN:AR_RED), 0);

    const char *name = v->alarm ? "** LEVEL UP! **" :
        v->paused ? "-- PAUSED --" :
        v->idle ? "** READY **" :
        (v->break_phase ? "~ BREAK ~" : "** FOCUS **");
    lv_label_set_text(s_status, name);

    char sc[28]; snprintf(sc, sizeof sc, "LVL %u  x%u", v->completed + 1, v->streak);
    lv_label_set_text(s_score, sc);
}

const theme_t THEME_ARCADE = {
    .name="Arcade", .bg=AR_BG, .accent=AR_RED, .cool=AR_GREEN,
    .build=ar_build, .update=ar_update,
};
```

- [ ] **Step 2: Build both targets**

Device build + sim build must link cleanly (paste tails + artifacts). Warnings must be clean.

- [ ] **Step 3: Commit**

```bash
git add src/ui/theme_arcade.c
git commit -m "feat(ui): Tomato Arcade theme (mascot + health bar + score)"
```

---

### Task 4: Flip Clock theme

**Files:**
- Modify: `src/ui/theme_flip.c` (replace the placeholder with the real face)

**Interfaces:**
- Consumes: `theme.h`, `timer_view.h`. Produces the real `THEME_FLIP`.

Design: warm bg (solid `0x221f1d` with a subtle vertical gradient to `0x141210`), two "flip cards" (dark rounded rects with a mid-seam line) showing MM and SS, warm cream digits (`0xf0e6d6`), a thin progress bar under them, and a lowercase prose status. Static cards updating their text (no mechanical flip animation — deferred as a Plan-polish nicety). Uses only obj/label/bar/style/gradient.

- [ ] **Step 1: Replace `src/ui/theme_flip.c`**

```c
#include "theme.h"
#include <stdio.h>

#define FL_BG1   0x221f1d
#define FL_BG2   0x141210
#define FL_CARD  0x141210
#define FL_SEAM  0x2e2a26
#define FL_TEXT  0xf0e6d6
#define FL_MUTED 0x8a7d6b
#define FL_ACCENT 0xC8503C

static lv_obj_t *s_mm, *s_ss, *s_status, *s_bar, *s_title;

static lv_obj_t *card(lv_obj_t *par, int x) {
    lv_obj_t *c = lv_obj_create(par);
    lv_obj_set_size(c, 150, 150);
    lv_obj_align(c, LV_ALIGN_CENTER, x, -14);
    lv_obj_set_style_bg_color(c, lv_color_hex(FL_CARD), 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    /* mid seam */
    lv_obj_t *seam = lv_obj_create(c);
    lv_obj_set_size(seam, 150, 3);
    lv_obj_align(seam, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(seam, lv_color_hex(FL_SEAM), 0);
    lv_obj_set_style_border_width(seam, 0, 0);
    return c;
}

static void fl_build(lv_obj_t *face) {
    s_title = lv_label_create(face);
    lv_obj_set_style_text_color(s_title, lv_color_hex(FL_MUTED), 0);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_16, 0);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 12);
    lv_label_set_text(s_title, "focus");

    lv_obj_t *cm = card(face, -84);
    s_mm = lv_label_create(cm);
    lv_obj_set_style_text_color(s_mm, lv_color_hex(FL_TEXT), 0);
    lv_obj_set_style_text_font(s_mm, &lv_font_montserrat_48, 0);
    lv_obj_center(s_mm); lv_label_set_text(s_mm, "25");

    lv_obj_t *cs = card(face, 84);
    s_ss = lv_label_create(cs);
    lv_obj_set_style_text_color(s_ss, lv_color_hex(FL_TEXT), 0);
    lv_obj_set_style_text_font(s_ss, &lv_font_montserrat_48, 0);
    lv_obj_center(s_ss); lv_label_set_text(s_ss, "00");

    /* colon */
    lv_obj_t *colon = lv_label_create(face);
    lv_obj_set_style_text_color(colon, lv_color_hex(FL_TEXT), 0);
    lv_obj_set_style_text_font(colon, &lv_font_montserrat_48, 0);
    lv_obj_align(colon, LV_ALIGN_CENTER, 0, -20);
    lv_label_set_text(colon, ":");

    s_bar = lv_bar_create(face);
    lv_obj_set_size(s_bar, 300, 8);
    lv_obj_align(s_bar, LV_ALIGN_CENTER, 0, 84);
    lv_bar_set_range(s_bar, 0, 1000);
    lv_bar_set_value(s_bar, 1000, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(FL_CARD), LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(FL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, 4, LV_PART_INDICATOR);

    s_status = lv_label_create(face);
    lv_obj_set_style_text_color(s_status, lv_color_hex(FL_MUTED), 0);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_16, 0);
    lv_obj_align(s_status, LV_ALIGN_CENTER, 0, 108);
    lv_label_set_text(s_status, "ready when you are");
}

static void fl_update(const timer_view_t *v) {
    unsigned s = v->rem_ms/1000;
    char mm[4], ss[4]; snprintf(mm, sizeof mm, "%02u", s/60); snprintf(ss, sizeof ss, "%02u", s%60);
    lv_label_set_text(s_mm, mm);
    lv_label_set_text(s_ss, ss);

    int32_t val = v->total_ms ? (int32_t)((uint64_t)v->rem_ms*1000/v->total_ms) : 0;
    lv_bar_set_value(s_bar, val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(v->break_phase?FL_MUTED:FL_ACCENT), LV_PART_INDICATOR);

    lv_label_set_text(s_title, v->break_phase ? "break" : (v->idle||v->alarm ? "pomodoro" : "focus"));
    const char *st = v->alarm ? "time's up — take a breath" :
        v->paused ? "paused" :
        v->idle ? "ready when you are" :
        (v->break_phase ? "rest your eyes" : "stay with it");
    lv_label_set_text(s_status, st);
}

const theme_t THEME_FLIP = {
    .name="Flip Clock", .bg=FL_BG1, .accent=FL_ACCENT, .cool=FL_MUTED,
    .build=fl_build, .update=fl_update,
};
```
Note: the warm vertical gradient background is applied by the shell using `s_theme->bg` as the solid color; the subtle `FL_BG2` gradient is a nicety the shell doesn't currently apply (the theme only exposes one `bg`). Keeping a solid warm bg is fine for B2; a gradient can be added later by extending `theme_t` with a `bg2`. Do NOT add gradient plumbing in this task.

- [ ] **Step 2: Build both targets** (device + sim link clean; paste tails + artifacts).

- [ ] **Step 3: Commit**

```bash
git add src/ui/theme_flip.c
git commit -m "feat(ui): Flip Clock theme (flip cards + warm palette)"
```

---

### Task 5: Cross-screen accent + integration pass

**Files:**
- Modify: `src/ui/screen_settings.c`, `src/ui/screen_nearby.c` (title color follows the active theme accent)

**Interfaces:** Consumes `theme_get`, `app()->settings.theme`.

- [ ] **Step 1: Tint the Settings and Nearby titles with the active theme accent**

In `src/ui/screen_settings.c` and `src/ui/screen_nearby.c`, include `theme.h`, and in each `*_update()` set the title label's text color to the active theme accent so the whole app reskins. For Settings, add near the top of `screen_settings_update()` (the function currently has an empty body — give it one):
```c
void screen_settings_update(void) {
    lv_obj_set_style_text_color(s_title, lv_color_hex(theme_get(app()->settings.theme)->accent), 0);
}
```
(Requires storing the title label pointer `s_title` as a file-static in screen_settings.c — it is currently a local in `screen_settings_create`; promote it to a file-static `static lv_obj_t *s_title;` and assign it there.)
For Nearby, add to the top of `screen_nearby_update()`:
```c
    lv_obj_set_style_text_color(s_title, lv_color_hex(theme_get(app()->settings.theme)->accent), 0);
```
(Promote its `title` local to a file-static `s_title` in `screen_nearby_create` as well.)

- [ ] **Step 2: Full host suite + both builds**

Run `powershell -File tools/test.ps1` (7/7). Device build + sim build link clean, no warnings. Paste all + artifacts.

- [ ] **Step 3: Sim launch smoke test**

Launch `build-sim/wilidoro_sim.exe` in the background ~4s, confirm alive (no crash), terminate. Report result. (No hardware.)

- [ ] **Step 4: User-checkpoint note**

In the report, write how to verify: run `tools/sim.ps1`; open Settings (Menu / red button), tap the **Theme** row (its ± cycles Neon Arc → Arcade → Flip Clock), press **Back** to the timer to see the new face; repeat. Note each theme's look: Neon = orange ring; Arcade = purple bg + tomato + health bar + "LVL/x"; Flip = warm flip cards. And that on-device flashing is a separate user-gated step.

- [ ] **Step 5: Commit**

```bash
git add src/ui/screen_settings.c src/ui/screen_nearby.c
git commit -m "feat(ui): cross-screen theme accent; Plan B2 integration pass"
```

---

## Self-Review

**Spec coverage (Plan B2 scope):** three switchable themes as full skins (Tasks 2–4), theme selection in Settings that changes the face live (Task 2 Step 3), and the cross-screen palette follow (Task 5). The spec's "each theme defines colors, fonts, widgets, animations, sound set, and LED patterns" is satisfied for colors/fonts/widgets; **animations, sounds, and LED patterns are explicitly deferred** (sounds/LEDs to Plan C per the A/B/C split; the flip mechanical animation is noted as a later nicety) — stated in Global Constraints and the task notes so they aren't mistaken for gaps.

**Placeholder scan:** No TBD/TODO in delivered code. Task 1 carries full test + impl. The Task 2 arcade/flip "placeholder" files are complete, compiling alias descriptors (not stubs) and are replaced wholesale in Tasks 3–4. Every theme's build/update is complete code using confirmed LVGL 9.2.2 signatures.

**Type consistency:** `theme_t` (name/bg/accent/cool/build/update) is identical across `theme.h`, the registry, and all three theme files. `timer_view_t` fields used by every theme's `update` match `timer_view.h` (Task 1). `theme_get`/`screen_timer_apply_theme`/`THEME_NEON|ARCADE|FLIP` names are consistent across screen_timer, screen_settings, theme.c, and the theme files. The refactored `screen_timer` keeps the exact softkey action logic from B1 (verified against the merged file) so no behavior regresses.

**Risks called out inline:** each theme file must compile warning-clean under `-Wall -Wextra` (the styled-object helpers `(void)`-nothing, all params used); the arcade/flip faces use `lv_bar` + styled `lv_obj` only (confirmed enabled). The `s_title` promotion in Task 5 must be done in both screen files or the update will not compile.

## Roadmap after B2

- **Plan C — Hardware features:** fill the device HAL stubs — per-theme LED patterns + completion celebrations (the theme_t can gain an LED hook), synthesized per-theme sounds (start/end/tick/blip), BMI323 gestures → `core/gestures` → app actions, OPT4001 → `core/dimming` → `hal_backlight` auto-dim, and CC1101 beacon TX (`beacon_ook_encode`→`ook_tx_send`) + RX populating the neighbor table for real. Also the deferred niceties: flip-card mechanical animation, the flip warm-gradient bg (`theme_t.bg2`), a `hal_led_count()` seam, and D-pad navigation in Settings.
