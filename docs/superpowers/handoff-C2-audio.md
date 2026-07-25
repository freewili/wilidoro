# Handoff prompt — Plan C2 (Audio)

Paste the block below into a new session (run from the wilidoro project dir).

---

I'm continuing work on **wilidoro**, a FreeWili 2 Pomodoro timer (C + LVGL 9 + the `wilibsp` BSP), at `C:\~prj\Dropbox\vibeProjects\wilidoro`. I want to design and build **Plan C2 — Audio**. Project memory should auto-load; also read `docs/superpowers/specs/2026-07-24-wilidoro-pomodoro-design.md` (the approved design) and skim the merged plans under `docs/superpowers/plans/`.

## Where things stand (all merged to `main`, host suite green)
- **Plan A** — foundation: pure host-tested `core/` (pomodoro FSM, beacon codec, gestures, dimming), device firmware + SDL simulator, blank LVGL screen. Hardware-verified.
- **Plan B1** — app shell: `app/app_model` (settings + neighbor table), both HALs, controller (200 ms tick), Timer/Settings/Nearby screens. Hardware-verified.
- **Plan B2** — themes: `theme_t` interface + `timer_view` + three skins (Neon Arc / Tomato Arcade / Flip Clock), live theme switching.
- **Plan C1** — ambient: `app/led_pattern` (per-theme LED render), OPT4001 auto-dim → `core/dimming` → backlight + LED brightness. LED brightness capped at `LED_BRIGHT_MAX = 40` in `src/app/app.c`.

## The established workflow (follow it)
1. Design/spec is DONE — go straight to the **superpowers:writing-plans** skill.
2. **Plan C is decomposed** into C1 (done), **C2 (audio, this one)**, C3 (gestures), C4 (radio beacon). Write a focused Plan C2 (a handful of tasks).
3. Execute with **superpowers:subagent-driven-development**: create a branch, a ledger at `.superpowers/sdd/progress-c2.md`, then per task: generate the brief with `.claude/plugins/cache/.../subagent-driven-development/scripts/task-brief PLAN N`, dispatch a fresh implementer subagent (haiku for verbatim-code/TDD tasks, sonnet for integration/build tasks — always specify the model), then `scripts/review-package BASE HEAD` + a sonnet reviewer per task; fix findings; final whole-branch review on **opus**; then **superpowers:finishing-a-development-branch** to merge.
4. Commit trailer on every commit: `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.

## Conventions
- **HAL is the only hardware seam.** `src/hal/hal.h` declares it; `hal_target.c` (device, wilibsp) and `hal_sim.c` (SDL) implement it. `app/`, `ui/`, `core/` never call wilibsp/SDL directly.
- **Pure logic is host-tested** (CTest + `tests/greatest.h`, run `powershell -File tools/test.ps1`). Put any C2 pure logic (e.g. a tone/envelope table, an event→sound mapping) in `src/app/` LVGL-free and TDD it.
- **Builds:** device `powershell -File tools/build.ps1 -Clean` (→ `build/wilidoro.uf2`, must link no SRAM overflow, no warnings; ~10 min); sim `cmake -G Ninja -B build-sim -S src/sim -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe -DCMAKE_PREFIX_PATH=C:/msys64/mingw64` then `cmake --build build-sim`.
- **ASK BEFORE TOUCHING HARDWARE.** Never flash / RTT / debug-probe / eMeet-camera without explicit user go-ahead (see the `ask-before-hardware` memory). Verify C2 via host tests + both builds + sim launch smoke; batch any on-device audio check for a user-gated session.

## Plan C2 scope (Audio)
Make audio real: implement `hal_tone(uint16_t hz, uint16_t ms, uint8_t amp)` and `hal_audio_idle(void)` on the **device** using the wilibsp audio stack, and give the app per-theme synthesized sounds. Today `hal_tone` is a no-op stub on both targets; the controller (`src/app/app.c` tick) already calls `hal_tone(880,200,200)` on focus-end (alarm) and `hal_tone(660,120,160)` on break-end as placeholders.
- **Sounds (synthesized, tone + envelope, NO sample assets):** per-theme start chime, end-of-focus alarm, gentle break-end chime, optional quiet focus tick, subtle touch/softkey blips. Keep them keyed by theme index (0 neon / 1 arcade / 2 flip), consistent with the LED palette convention.
- **Volume:** wire the existing `app_settings_t.volume` (0..100) to scale amplitude.
- **Speaker is 0.5 W** — bound levels and power the codec down when idle (`hal_audio_idle` / `codec_nau88c10_speaker_low_power()`).
- **Sim:** keep `hal_tone`/`hal_audio_idle` as no-ops (or a console log); audio isn't the sim's job.

## Research FIRST (before writing the plan)
Read the actual local headers/docs for exact signatures & invariants:
- `wilibsp/bsp/audio/{codec_nau88c10,audio_i2s_duplex,tone_gen}.h` (init, play_loop, tone_gen_fill).
- `wilibsp/AGENTS.md` + `wilibsp/docs/hardware/facts.md` + `wilibsp/docs/drivers/audio.md` for the audio invariants: 250 MHz clock → MCLK = clk_sys/61 (fs ≈ 16 kHz); I2S on pio0; DMA_IRQ_0 is **shared** (use `irq_add_shared_handler`, never exclusive); the play buffer must be power-of-2 sized AND aligned to its size; speaker 0.5 W limit; codec starts DAC soft-muted. The `hello_audio` app is the reference pattern.
- Confirm whether `hal_tone` should synthesize a one-shot tone (fill a small aligned buffer, play for `ms`, then stop/idle) — figure out the cleanest one-shot model over `audio_i2s_duplex_play_loop` (which loops); you may need a short play + timed stop, or a small non-looping play. Decide this in the plan.

## Known seams / notes
- OPT4001 (C1) and the future BMI323 (C3) share I2C — not C2's concern, but don't break the shared bus.
- Backlog: `hal_beacon_tx` still inert until C4; D-pad/OK buttons dropped by the controller (touch-only settings edit on device); `tick_cb` is accreting (consider extracting helpers).

Start by reading the memory + spec + the wilibsp audio headers, then use the writing-plans skill to draft Plan C2.
