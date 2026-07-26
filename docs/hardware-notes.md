# Wilidoro Hardware Notes

Bench-discovered tuning defaults for the FreeWili 2 hardware. The `wilibsp`
BSP is a pinned upstream git submodule, so project-specific notes live here
rather than inside it (see the note at the bottom).

## WS2812 LEDs — brightness

The 16 WS2812 LEDs are **extremely** bright — full strength is uncomfortable
indoors. A comfortable indoor default is a brightness **ceiling of ~40/255**
(about 1/6 of full).

Wilidoro caps the auto-dim-derived LED brightness at `LED_BRIGHT_MAX = 40` in
`src/app/app.c`; the OPT4001 auto-dim path (`core/dimming`) still scales it
*lower* than that in a dim room, so 40 is the bright-room maximum, not a fixed
value. Verified comfortable on real hardware, 2026-07-25.

If you drive the strip directly via the BSP (`ws2812_set_brightness(level)`),
~40 is a good default `level`; the BSP's own `hello_display` uses 64, which is
already on the bright side for close-up desk use.

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
bench-comfort default in the same spirit as `LED_BRIGHT_MAX = 40`; it was
level-checked on real hardware on 2026-07-25 and needed no adjustment.

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

### Hardware bring-up, 2026-07-25 — audio VERIFIED working

Flashed and confirmed audible on real hardware. Two independent faults had to be
cleared first, and both are worth knowing about:

1. **The speaker jumper must be connected.** With it off there is no sound at
   all, whatever the firmware does. Check this first — it costs seconds, and it
   masks every other symptom.
2. **A BSP bug: an undrained I2S RX FIFO wedged the PIO state machine**, so every
   note came out as a *click* instead of a tone. Fixed upstream in `wilibsp`
   (`7b5a680`; symptom description corrected in `2e5ef86`) — see
   `wilibsp/docs/hardware/facts.md`. wilidoro itself needed no code change, only
   the submodule bump.

Verified at boot: RTT reports `codec: rev(0x3F)=0x01A pm2(0x02)=0x015` then
`audio: codec ok`, so `hal_caps().audio` is true and tones are enabled. (Note
`codec_nau88c10_input_ok()` gates on the ADC/mic path as well as the silicon
revision — it passes here, but it over-tests for a playback-only app.)

Confirmed by ear: the per-theme **start chimes** are good and the level is
right — `TONE_AMP_CAP = 160` needed no adjustment for them. (The focus-end
alarm, which holds the loudest table entries at amp 215, was not played this
session; see "Not yet exercised" below.) Confirmed by mic + Goertzel analysis:
clean sustained notes at a sound-table frequency (787 Hz measured against the
781.7 Hz the HAL emits for the 784 Hz table entry), magnitude ~1100 versus a
silent-control floor of 0.0.

**Fingerprint worth remembering:** *clicks instead of tones, with every codec
register reading correct* means the PIO state machine is stalling, not that the
codec is dead. Confirm by reading `PIO0->FDEBUG` (`0x50200008`): `RXSTALL` is
bits 3:0 and sticky.

Not yet exercised (needs a longer session; none is a blocker):

- The focus-end alarm re-ringing every 5 s until **Dismiss**.
- Volume 0 fully silent, 50 audibly quieter than 100.
- The focus tick firing once a minute, and never while paused.
- Whether the low notes (523/659 Hz) carry on this small speaker as well as the
  784/1047 Hz ones. If they don't, pitch the tables higher rather than raising
  `TONE_AMP_CAP` — a 0.5 W speaker has very little low end.
- Listening for a residual tone or DC click across rests (`hz == 0`) and at
  sequence end. Only the Flip and Arcade alarms have rests, and neither has
  been exercised yet, so this is still open.

## Themes — all three confirmed on the panel

*Hardware-verified 2026-07-26, camera-captured on an eMeet C960. Closes the Plan B2
item "flash, then cycle themes via Settings", open since 2026-07-25.*

All three themes render correctly on the real panel, and **switching theme live from
the Settings row works** — the face rebuilds without a reboot.

| theme | confirmed on screen |
|---|---|
| Neon Arc | orange arc ring, `MM:SS` centred, `FOCUS n/4` |
| Retro Tomato Arcade | tomato mascot + green stalk, `lv_bar` health bar, `LVL n xN` score, `** FOCUS **` |
| Warm Flip Clock | two flip cards with the mechanical seam, lowercase `focus` status, session dots, `stay with it` prose |

The **softkey bar stayed byte-identical across all three**, which is the
theme-independence B2 specified rather than something that merely happened to look
similar.

**The arc geometry is right, not just plausible:** a capture reading `03:47` showed the
ring ~85 % complete, and 21:13 elapsed of a 25:00 focus is 85 %. The permille math
checks out against the clock.

**On the Flip Clock palette — resolved, and worth recording so nobody "fixes" it.**
The first captures made the Flip Clock face look *cool*, which seemed to contradict the
design's "Warm Flip Clock". Cropping the frame to just the panel — cutting out the
magenta WS2812 wash that was skewing the camera's white balance — shows the warm coral
accent clearly. Reading the constants confirms the intent: `FL_ACCENT = 0xC8503C` is a
distinctly warm coral/rust, and `FL_BG1 = 0x221f1d` is a warm-biased near-black. The
card fill simply *is* nearly black, so a 5-unit warm bias in it is imperceptible either
by eye or on camera; the warmth in this theme lives in the accent, not the background.
Nothing to change in `theme_flip.c`.

The general lesson: **an uncropped board photo is not usable evidence about on-screen
colour.** The LEDs sit centimetres from the panel and dominate the frame's white
balance. Crop to the panel before judging any palette.

## DVI output — region size and pixel clock

*Hardware-verified 2026-07-25: picture confirmed on a mini projector (HDMI input)
at the board default 250 MHz — i.e. at the 0.7 %-low 25.0 MHz pixel clock, with
no clock change needed.*

The RP2350 HSTX block drives 640×480p60 DVI on GPIO 12–19. Wilidoro shows a
room-readable focus view there — state word, huge `MM:SS`, session dots — in the
active theme's colors. The LCD is unaffected and remains the control surface.

**Pixel clock is `clk_sys/10`.** At the board default 250 MHz that is 25.0 MHz —
0.7 % below the 25.175 MHz standard, and **confirmed on hardware to be within
tolerance**: a mini projector synced and displayed the view at 25.0 MHz with no
clock change. This keeps the NAU88C10 audio at exactly the 16009 Hz it was
verified at. `board_init_clk(252000)` would give
an exact 25.2 MHz at the cost of ~0.8 % audio pitch; the sample rate is now
derived from `clk_sys` at runtime, so that switch is safe to make if a monitor
refuses to sync.

**The region is 480×240 = 244 KB**, set by
`target_compile_definitions(freewili2_bsp PUBLIC HSTX_VID_W_MAX=480 HSTX_VID_H_MAX=240)`
in the root `CMakeLists.txt`. Those macros size `framebuf[]` at **compile** time —
passing a smaller `vid_h` to `hstx_dvi_init()` does not shrink it. The BSP default
480×320 is 320 KB, which with our other ~187 KB of BSS leaves ~5 KB for heap
and will not fit. (The `#ifndef` guards that make them overridable were added
upstream in `wilibsp`.)

**The framebuffer is strided**: rows are separated by HSTX scanout command words,
so row `y` starts at `hstx_dvi_video_base() + y*hstx_dvi_video_stride()` and is
only `hstx_dvi_video_w()` pixels wide. Writing past the width corrupts the scanout
program. `src/app/dvi_view.c` funnels every write through one clipped `fill_rect`,
and its unit tests render into a surface whose slack columns hold a sentinel value
to prove nothing escapes.

### On-device DVI checklist

Item 1 is **DONE** (2026-07-25). The rest still need a session with the display
connected.

1. **PASS** — a mini projector on HDMI synced and showed the view at 25.0 MHz,
   i.e. at the board default 250 MHz with no clock change. If some other display
   refuses, `board_init_clk(252000)` gives an exact 25.2 MHz; confirm the chimes
   still sound correct afterwards, since that moves fs to 16137 Hz.
   **Diagnostic note:** an earlier "no sync" at this same clock turned out to be
   an unplugged HDMI connector, and cost a wasted round-trip through 252 MHz.
   Before drawing any conclusion from a blank display, confirm the cable is
   seated at both ends and the input is selected — the same class of mistake as
   the speaker jumper in the audio section above.
2. The countdown is legible across a room and no digits are clipped.
3. Switching theme on the LCD recolors the DVI output live.
4. The Settings "DVI output" toggle blanks and restores it.
5. Audio still plays correctly with DVI scanout running — the scanout DMA adds
   continuous memory-bus traffic alongside the audio TX DMA and the blocking
   ST7796 flush.
6. The LCD refresh is not visibly degraded by that same contention.
7. With DVI previously toggled off, press Settings → Default and confirm the
   output returns (regression test for the Default-desyncs-`dvi_on` fix).

## Tilt to pause — thresholds and hold

*Hardware-verified 2026-07-25: flashed to a physical FreeWili 2 (RP2350 rev 3)
over the cmsis-dap probe, and the feature works — tipping the board up onto an
edge paused a running focus session, and setting it back down flat resumed it
from the same remaining time. The thresholds below were **not** adjusted; they
worked as chosen on the first try. See the checklist below for which individual
items were observed and which were not — several remain unverified.*

The board lying face-up and level is the running orientation; tilt it out of
level and a focus session pauses. `src/core/tilt.c` thresholds `az/‖a‖` — the
cosine of the tilt away from level — rather than raw `az`, because a 2 g jolt
can push raw `az` past the flat threshold while the board is nowhere near
level. This is orientation only, not motion: picking the board up and holding
it level does not pause it, because the gate never looks at `‖a‖` deviation,
only the direction of gravity.

| tunable | value | meaning |
|---|---|---|
| `TILT_FLAT_COS` | 0.87 | enter flat, within ~29.5° of level |
| `TILT_LIFT_COS` | 0.77 | leave flat, beyond ~39.7° |
| `TILT_HOLD_MS` | 600 | the new zone must persist this long to count |
| `TILT_MAG_MIN` | 0.30 | below this `‖a‖` the sample is discarded |

The 10 Hz `sensor_cb` gives 6 samples per hold window. These are bench-comfort
choices in the same spirit as `LED_BRIGHT_MAX = 40` and `TONE_AMP_CAP = 160`, and
they belong here rather than upstream in the BSP. If a lift is missed, lower
`TILT_LIFT_COS`; if the desk shaking pauses a session, raise `TILT_HOLD_MS`.

In practice `TILT_HOLD_MS` is not "persist this long" but "N consecutive
in-zone samples": any sample that lands back in the *previously committed*
zone clears the pending candidate and restarts the count, so 600 ms at the
100 ms cadence actually requires the 7th consecutive sample in the new zone.
This is stricter noise rejection than the table implies, not looser — it is
what keeps a reading that dithers right at the threshold from chattering.

**I2C1 is shared** by the BMI323 (0x68), the OPT4001, and the NAU88C10's control
registers. A single 100 ms `sensor_cb` in `src/app/app.c` owns every sensor read
on it — the IMU each call, lux every fifth — rather than two independent pollers.
All of it runs from LVGL timers on core 0, so this is a time-budget arrangement,
not a lock. A `bmi323_read` is 1 byte written plus 14 read, ~0.35 ms of bus time.
With the setting off, `hal_imu` is never called and the bus sees no extra traffic.

### On-device tilt checklist

Items 2, 3 and 4 are **DONE** (2026-07-25). The rest still need a session with
the board in hand. **Ask before flashing.**

1. **NOT READ.** RTT at boot should report `bmi323: chipid=0x0043 ok`. This was
   not captured — the bounded RTT script was refused by the tooling's permission
   layer, and item 2 answered the same question anyway (see below). If you do
   read it and it says `??`, the part *answered* but with an unexpected chip ID:
   `bmi323_init()`'s return value only reflects whether the bus read got an ACK,
   not whether the ID matched, so Settings still shows `on`/`off` as normal.
   Only a bus NAK (no ACK at all) clears `caps.imu` and makes Settings show
   `no imu` — check which of the two you have before suspecting the gate logic.
2. **PASS** — Settings showed **Tilt to pause** as `off`, and switching it to
   `on` stuck. Reading `off` rather than `no imu` is itself proof the BMI323
   ACK'd on I2C1, which is why item 1 was not needed to establish the part is
   alive.
3. **PASS** — with it on and a focus session running, tipping the board up onto
   its edge paused within ~600 ms, and the blip sounded (so `SND_BLIP` fires
   correctly from `sensor_cb`, not just from softkeys). Picking the board
   straight up and holding it level in hand is *expected* not to pause it — the
   gate reads only the direction of gravity, not that the board left the desk —
   so do not report that as a bug.
4. **PASS** — setting it back down flat resumed from the same remaining time,
   not from the top.
5. **NOT CONFIRMED.** A manual Pause taken while the board is flat must **not**
   be undone by the gate. This is the load-bearing edge-trigger property, and it
   is the most valuable item left on this list. It *is* covered on the host at
   the gate level (`flat_again_reports_once` and
   `primes_silently_then_lifts_once` in `tests/test_tilt.c` prove `tilt_feed`
   never emits for the standing zone), so this check is confirmation rather than
   sole evidence — but it has not been seen on hardware.
6. Resting it on a shallow stand (~35°, inside the dead band) neither pauses nor
   resumes, and does not oscillate.
7. With the setting off, tilting does nothing at all.
8. A break is never paused by tilting, only focus.
9. Auto-dim still tracks the room after the lux poll moved from `tick_cb` into
   `sensor_cb` — cover the sensor and confirm the backlight and LEDs drop.
   Note the poll is now a true 500 ms rather than the effective ~600 ms it ran
   at from `tick_cb`, so auto-dim converges about 17 % faster than before.
10. Pressing **Default** while a session is tilt-paused strands the pause: it
    is expected to require a manual Resume afterward, since Default turns
    `tilt_pause` off and re-primes the gate.

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
| `BEACON_TX_MS` | 20 000 | `src/app/app.c` | base transmit period |
| `BEACON_TX_JITTER_MS` | 3 000 | `src/app/app.c` | spreads the period to 18.5–21.5 s so two co-located units cannot lock into permanent mutual collision; `NEIGHBOR_TTL_MS` is 60 s, so a listener gets three chances at that jittered period |
| `BEACON_HALFBIT_US` | 500 | `src/core/beacon.h` | OOK half-bit, so a frame is ~136 ms |
| `BEACON_GAP_US` | 2 250 | `src/core/beacon_rx.h` | inter-frame gap threshold; `beacon_ook_decode` rejects runs whose rounded half-bit count exceeds 4, i.e. at 2 250 µs, so the threshold sits right at that boundary rather than past it |

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

### Why the framer flushes on a quiet line instead of waiting for a gap

**Do not "simplify" `beacon_rx_flush` away.** It looks redundant — `beacon_rx_push`
already closes a segment when a gap-length run arrives — but that gap never comes.

`gdo_capture.pio` timestamps a run **only when the run ends**, at the next level
transition. `ook_tx_send` finishes a burst with `gpio_put(GDO0, 0)` and nothing
touches the pin afterwards, so the burst's trailing low run is never pushed. Waiting
for it decodes **nothing**: 0 of 256 payloads, measured on a host model of the PIO's
emission rule. `tests/test_beacon_rx.c` models that rule precisely and asserts both
directions — a modelled capture decodes *with* a flush and not without — so the
regression is pinned. An earlier version of this feature waited for the gap and was
completely non-functional while passing every test, because the tests fed it the
encoder's output rather than the capture hardware's.

**Checked against the upstream source this driver was harvested from**
(`github.com/freewili/subghz`, local checkout at
a sibling checkout of it), because a working OOK receiver would be
better evidence than reasoning:

- `gdo_capture.pio` and `ook_tx.c` are **byte-identical** to the BSP's. The
  emission-on-transition behaviour is the original design, not something the harvest
  broke or a dropped timeout push.
- `monitor_engine` closes a burst exactly the way the broken version did —
  `ticks >= MON_IDLE_TICKS` (20 ms) — and that is fine *there*, because it drives a
  live pulse-width histogram with no deadline: the long run eventually gets pushed
  when the next edge (AGC noise or the next burst) arrives, and latency does not
  matter for a statistics display.
- **subghz never decodes frames at all.** It is capture-and-replay: it stores raw
  duration timelines and re-transmits them. Grepping its whole source for
  Manchester/CRC/decode finds nothing.

So there is no upstream precedent to copy for what wilidoro needs — a decoder that
must know whether a *complete* frame has arrived by now. The flush is that answer,
and the upstream example validates the hardware model without offering an
alternative.

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

### Two things inherent to the design (not defects)

1. **Self-reception is expected on this hardware.** PIO2 samples the GDO0 pad
   regardless of who is driving it — the same property the loopback self-test
   below relies on — so every frame this device transmits also lands in its
   own capture ring. This is why `hal_beacon_tx` drains the capture ring dry
   and re-initialises the framer after every transmit: without that, the
   device would decode its own beacon and list itself on its own Nearby
   screen.
2. **The beacon is unauthenticated and trivially spoofable.** There is no
   pairing, signing, or origin check — anything transmitting valid OOK
   Manchester on 433.92 MHz in this wire format can claim any name and any
   focus state. Treat the Nearby screen as informational among trusted
   nearby devices, not as any kind of identity guarantee.

### On-device beacon checklist

Flashed 2026-07-26 (merge `58998b4`). The board **boots and renders, audio plays,
and the LEDs animate** with the C4 firmware on it — so nothing in this branch
regressed the previously-verified display, audio or LED paths. Items 1 and 2 were
**not read**, and that gap is load-bearing: see the caveat under item 3. **Ask
before flashing.**

1. **NOT READ, and lower value than it first appears.** RTT at boot should report
   `cc1101: PARTNUM=0x00 VERSION=0x14` and `radio: cc1101 ok`.

   **The CC1101 is soldered to the board**, so this is not a presence check in any
   useful sense — there is no module to be missing. A `VERSION` of `0x00`/`0xFF`
   would mean the SPI *read* came back implausible, and with a soldered part that
   means a bus problem, not a missing chip. The one plausible mechanism is that
   **GPIO8 is shared**: it is the CC1101's MISO *and* the LCD's DC line.
   `spi_bus_acquire_cc1101()` re-muxes it per transaction, drops the baudrate from
   the LCD's 100 MHz to the radio's 5 MHz, and drains the SPI RX FIFO — the LCD
   does write-only transfers and never reads, so the FIFO holds LCD garbage that
   would otherwise return as the radio's first reply.

   `st7796_init()` runs at `main.c:20`, before `hal_init()` at `:25`, but LVGL does
   not start *flushing* until the main loop — so the boot probe exercises the GPIO8
   mux and the stale-FIFO drain (valuable) but **not** concurrency with an in-flight
   flush. Expect it to pass; it is the easy case.
2. RTT reports `beacon: loopback ok`. Because the self-test cannot false-pass,
   a pass is strong evidence the whole chain (pack → encode → TX timing → PIO2
   capture → framer → decode → unpack) works. A `FAILED` does **not** by
   itself point at `beacon_pack`/`beacon_unpack` — first rule out the
   capture-ring/flush behaviour (debris left in the ring from a previous run,
   or the flush that closes the segment not firing) before suspecting the
   codec.
3. **PASS, conditionally** — a softkey blip sounded cleanly with the C4 firmware
   flashed (2026-07-26). **The condition matters and is easy to miss:**
   `gdo_capture_init()` / `gdo_capture_start()` live *inside* `if (s_radio)` in
   `hal_init`, so **PIO2 only runs if the CC1101 answered.** If it did not, this
   observation proves only that C4 broke nothing — it does *not* exercise
   three-PIO coexistence, because there was no third PIO. Item 1's RTT line is
   what turns this from "nothing regressed" into "PIO0 + PIO1 + PIO2 coexist on
   silicon", which is a fact the BSP itself does not yet have.
4. **PASS, under the same condition as item 3** — the LEDs still animate (PIO1),
   with the same dependency on whether PIO2 was actually started.
5. **THE HIGHEST-VALUE TEST ON THIS LIST, and nothing has ever exercised it.**
   Turn **Beacon** on and watch the timer face for a few transmits with the sound
   on. Two independent things are under test at once:

   - **The contended GPIO8 mux.** Every transmit takes SPI1 away from the display
     and re-muxes GPIO8 (LCD DC ↔ CC1101 MISO) *while LVGL is actively flushing*.
     `spi_bus_acquire_cc1101()` spins on `st7796_flush_busy()` for exactly this, and
     **wilidoro is the first app where that spin can actually block**, because
     `hello_cc1101` has no display. Display corruption, tearing, or a wrong-looking
     colour right after a transmit is this failing.
   - **The 136 ms core-0 stall.** It must not visibly disrupt the countdown, and the
     `!sound_active()` gate must keep it from ever stretching a chime.

   Three transmits (~1 minute) is enough to know. This matters more than item 1: the
   boot probe is the uncontended case, this is the contended one.
6. With **Beacon** off (the default), there are no *periodic* transmits. The
   boot self-test still fires once regardless of the setting
   (`radio_loopback_selftest` runs from `hal_init`, before `app_init` reads
   any setting at all) — one `beacon: loopback` frame at every boot, off or
   on, is expected and is not a bug. Beacon off just means no further
   transmit ever follows that one.
7. Auto-dim, touch and the tilt gate all still behave — the transmit stall must
   not break the 100 ms `sensor_cb` cadence beyond a skipped sample.
8. **Worth fixing, and it would have closed items 1/3/4 without RTT.** The Settings
   **Beacon** row shows only `on`/`off` — it does not surface `hal_caps().radio`,
   so there is no way to tell from the screen whether the CC1101 answered. The
   **Tilt to pause** row already does exactly this, showing `no imu` when
   `hal_caps().imu` is false (added in Plan C3). Giving Beacon the same treatment
   (`no radio`) is a few lines in `screen_settings.c`'s `refresh_values`, and it
   would make the radio's presence self-evident on the device instead of requiring
   a probe and an RTT capture. The `Beacon` row predates that pattern; it was never
   deliberately excluded.
9. **Conditional on a second transmitter.** A FreeWili One is also on the bench,
   but `wilibsp` only supports `freewili2`, so it cannot run wilidoro firmware.
   If it carries a 433 MHz CC1101 and its own tooling can send raw OOK, the real
   two-device test becomes available: one board transmitting, the other listing it
   on the Nearby screen. Until then the Nearby screen showing nothing on hardware
   is expected and proves nothing either way.

---

**Why this note isn't in the BSP:** `wilibsp/` is a git submodule
(`github.com/freewili/wilibsp`). Project-specific *tuning defaults* — the LED
brightness ceiling and the audio amplitude cap above — are bench-comfort
choices for this particular product, not BSP-level facts, so they belong here
rather than in a driver header that other wilibsp consumers share. Genuine BSP
*bugs*, by contrast, are in scope to fix upstream: two examples from this plan
show the pattern. An RP2350-E5 DMA erratum in `audio_i2s_duplex_play_stop()`
was fixed in wilibsp (`849ec60`, `fix(audio): use dma_channel_cleanup() to stop
chained I2S TX DMA`), and the lack of HSTX framebuffer sizing was fixed in
`781208b` (`feat(dvi): let apps size the HSTX framebuffer`); both were pushed to
`github.com/freewili/wilibsp` master and this repo's submodule pins were bumped
to them (`833c1bd` and `092f5ca` respectively) — that pattern is the template
for any future BSP-bug fix. Ask before opening upstream PRs for new tuning-style
notes like the ones in this file, but bugs get fixed upstream as a matter of course.
