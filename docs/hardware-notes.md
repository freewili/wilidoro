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
8. Listen across the rests in the Flip and Arcade alarms, and at the very end of
   each sequence, for a residual tone or DC click:
   `audio_i2s_duplex_play_stop()` clears the PIO TX FIFO on a rest (`hz == 0`)
   or sequence end, and with autopull enabled an empty FIFO can leave the state
   machine stalled holding its last sample rather than provably driving
   mid-scale — this is only checkable by ear, not from source.

---

**Why this note isn't in the BSP:** `wilibsp/` is a git submodule
(`github.com/freewili/wilibsp`). Project-specific *tuning defaults* — the LED
brightness ceiling above, the audio amplitude cap below — are bench-comfort
choices for this particular product, not BSP-level facts, so they belong here
rather than in a driver header that other wilibsp consumers share. Genuine BSP
*bugs*, by contrast, are in scope to fix upstream: a real RP2350-E5 DMA erratum
in `audio_i2s_duplex_play_stop()` was found during this plan, fixed in wilibsp
(`849ec60`, `fix(audio): use dma_channel_cleanup() to stop chained I2S TX DMA`),
pushed to `github.com/freewili/wilibsp` master, and this repo's submodule pin
was bumped to it (`833c1bd`) — that commit pair is the template for any future
BSP-bug fix. Ask before opening upstream PRs for new tuning-style notes like
the ones in this file, but bugs get fixed upstream as a matter of course.
