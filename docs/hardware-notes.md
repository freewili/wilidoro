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

---

**Why this note isn't in the BSP:** `wilibsp/` is a git submodule
(`github.com/freewili/wilibsp`). Project-specific *tuning defaults* — the LED
brightness ceiling and the audio amplitude cap above — are bench-comfort
choices for this particular product, not BSP-level facts, so they belong here
rather than in a driver header that other wilibsp consumers share. Genuine BSP
*bugs*, by contrast, are in scope to fix upstream: a real RP2350-E5 DMA erratum
in `audio_i2s_duplex_play_stop()` was found during this plan, fixed in wilibsp
(`849ec60`, `fix(audio): use dma_channel_cleanup() to stop chained I2S TX DMA`),
pushed to `github.com/freewili/wilibsp` master, and this repo's submodule pin
was bumped to it (`833c1bd`) — that commit pair is the template for any future
BSP-bug fix. Ask before opening upstream PRs for new tuning-style notes like
the ones in this file, but bugs get fixed upstream as a matter of course.
