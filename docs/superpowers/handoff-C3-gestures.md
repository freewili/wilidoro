# Handoff prompt — wilidoro, next session (Plan C3: IMU gestures)

Paste the block below into a new session, run from the wilidoro project dir.

---

I'm continuing work on **wilidoro**, a FreeWili 2 Pomodoro timer (C + LVGL 9 + the
`wilibsp` BSP), at `C:\~prj\Dropbox\vibeProjects\wilidoro`. Project memory should
auto-load. Read `docs/superpowers/specs/2026-07-24-wilidoro-pomodoro-design.md`
(the approved design) and `docs/hardware-notes.md` (bench-verified tuning values
and the on-device checklists).

## FIRST: resolve the unmerged branch

`plan-dvi-output` may still be unmerged. Check `git branch` and `git log --oneline main..plan-dvi-output`.
It is complete, fully reviewed and hardware-verified — host 10/10, device build
543/543 zero warnings, DVI confirmed on a projector. If it is still there, ask me
whether to merge it before starting anything new.

## Where things stand (all merged to `main` unless noted)

- **Plan A** — foundation: host-tested `core/` (pomodoro FSM, beacon codec,
  gestures classifier, dimming), device firmware + SDL simulator. Hardware-verified.
- **Plan B1** — app shell: `app_model`, both HALs, 200 ms controller tick,
  Timer/Settings/Nearby screens. Hardware-verified.
- **Plan B2** — three themes (Neon Arc / Tomato Arcade / Flip Clock) behind a
  `theme_t` interface + pure `timer_view`.
- **Plan C1** — ambient: `app/led_pattern`, OPT4001 auto-dim → backlight + LED
  brightness. `LED_BRIGHT_MAX = 40`.
- **Plan C2** — audio: **DONE + hardware-verified.** Pure `app/sound` (per-theme
  note tables + sequencer), real `hal_tone`/`hal_audio_idle` over the NAU88C10
  I2S DMA ring, 20 ms LVGL timer drains the sequencer, alarm re-rings every 5 s
  until dismissed. `TONE_AMP_CAP = 160/255` for the 0.5 W speaker. User confirmed
  "the sounds are good".
- **DVI output** — **DONE + hardware-verified** (branch `plan-dvi-output`, see
  above). 640×480p60 over HSTX showing a room-readable focus view: pure
  host-tested `app/dvi_view` (7-segment `MM:SS`, state word, session dots, per-theme
  colours), `hal_dvi_surface`/`hal_dvi_enable` seam, 480×240 region (244 KB),
  Settings toggle, and a second SDL window in the simulator. Confirmed working on
  a mini projector at the board default 250 MHz.

**Still to do — Plan C3 and C4** (both specced in the design doc, neither started):

- **C3 — IMU gestures.** BMI323 → `hal_imu` → `core/gestures` (already written and
  host-tested in Plan A) → app actions: flip face-down starts focus / enters
  deep-focus, flip up restores, shake dismisses the alarm, pick-up wakes the dim
  screen. **Important:** the OPT4001 (C1) and BMI323 share I2C1. C3 needs a single
  shared read cadence, not a second independent blocking poller.
- **C4 — CC1101 beacon.** Real TX (`beacon_ook_encode` → `ook_tx_send`) gated by
  `beacon_on`, plus RX (`gdo_capture` → `beacon_ook_decode`) populating the
  neighbour table. Requires migrating the LVGL flush from the blocking
  `st7796_blit_rect` to `st7796_flush_async` plus SPI1 arbitration, since the radio
  shares SPI1. ~81 KB of SRAM headroom remains; another 38 KB is available by
  dropping LVGL's second draw buffer in `src/target/lvgl_port.c` if needed.

## The workflow (follow it)

1. The overall design is approved. For a new feature, use **superpowers:brainstorming**
   → write a spec → **superpowers:writing-plans** → **superpowers:subagent-driven-development**.
2. SDD: branch + a ledger in the plan's workspace; per task run `scripts/task-brief`,
   dispatch a fresh implementer (haiku when the plan contains the full code, sonnet
   for integration), then `scripts/review-package` + a sonnet reviewer. Final
   whole-branch review on **opus**, one fix wave, one scoped re-review, then
   **superpowers:finishing-a-development-branch**.
3. Commit trailer on every commit: `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`

## Conventions

- **HAL is the only hardware seam.** `src/hal/hal.h`; `hal_target.c` (wilibsp) and
  `hal_sim.c` (SDL). `app/`, `ui/`, `core/` never touch wilibsp or SDL.
- **Pure logic is host-tested.** `src/app/` and `src/core/` modules stay LVGL- and
  hardware-free so CTest can cover them. Suite is currently **10 binaries**;
  run `powershell -File tools/test.ps1`.
- **Builds:** device `powershell -File tools/build.ps1 -Clean` (~10 min, must be
  zero warnings, no RAM overflow); sim
  `cmake -G Ninja -B build-sim -S src/sim -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe -DCMAKE_PREFIX_PATH=C:/msys64/mingw64`
  then `cmake --build build-sim`.
- **ASK BEFORE TOUCHING HARDWARE** — flashing, OpenOCD/RTT, debug probe, camera,
  microphone. Builds, host tests and the simulator are free.

## Hard-won lessons — read these, they cost real time

1. **Check the physical setup before interpreting ANY negative hardware result.**
   This bit twice in one session: "no audio" was partly a disconnected speaker
   jumper, and "no DVI sync" was an unplugged HDMI connector that sent me through
   an unnecessary system-clock change. Before concluding anything from silence or
   a blank screen, confirm the jumper/cable/input. Ask the user explicitly.
2. **Never conclude from a recording without confirming the stimulus happened.**
   Twice I analysed microphone captures for chimes that were never triggered and
   nearly reported a regression. If you asked the user to press a button, verify
   they did — RTT is the cheap way (the BSP logs `codec: output -> speaker only`
   every time a tone is armed).
3. **Fixing `wilibsp` bugs upstream is in scope and expected** — see the
   `wilibsp-upstream-fixes` memory for the push-and-bump workflow. Three fixes
   have gone upstream already.
4. **Trust working code over prose.** `dvi.md` said 25.0 MHz is within tolerance
   while `hello_dvi` used 252 MHz; `facts.md` said DVI was unverified. I weighted
   the prose and got it backwards — though in the end the prose was right and the
   real fault was the cable.
5. **`docs/hardware-notes.md` distinguishes verified from assumed.** Keep that
   discipline: say plainly what has been seen on hardware and what has not.

## Useful debugging techniques (see the `freewili-hardware-debugging` memory)

Bounded RTT capture (not `tools/rtt.ps1`, which streams forever), live peripheral
register probes over SWD with `openocd -c "mdw <addr>"` (no reset, no halt),
single-bit A/B tests on one unchanged firmware image, and microphone capture with
a stdlib Goertzel analysis against a silent control window. These turned guesses
into measured facts repeatedly.

## Suggested first step

Brainstorm **Plan C3 (IMU gestures)**, paying particular attention to the shared
I2C1 cadence with the OPT4001, and to which gestures are worth the complexity —
the design lists five, and some may not earn their keep.
