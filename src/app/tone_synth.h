/* Pure note -> 16-bit PCM renderer for the FreeWili OG's I2S output.
 *
 * The FreeWili 2's HAL hands a frequency to a codec; the OG's i2s_audio driver
 * takes sample buffers, so the samples have to come from somewhere. Keeping
 * that somewhere pure is what lets the band-limiting and the buffer bounds be
 * tested on a host with no board.
 *
 * No SDK, no hal, no LVGL: standard C only. */
#ifndef WILIDORO_TONE_SYNTH_H
#define WILIDORO_TONE_SYNTH_H
#include <stddef.h>
#include <stdint.h>

/* The OG's I2S block is fixed at 8 kHz, 16-bit (i2s_audio.c derives its PIO
   divider from clock_get_hz(clk_sys) against this rate). */
#define TONE_RATE_HZ     8000u

/* sound.h caps a note at 400 ms, which is 3200 samples here. */
#define TONE_MAX_MS      400u
#define TONE_MAX_SAMPLES ((TONE_RATE_HZ * TONE_MAX_MS) / 1000u)   /* 3200 */

/* Nyquist is 4 kHz. sound.h's shared note tables go to SOUND_HZ_MAX 3000,
   which alias badly here, so the OG clamps -- in the HAL rather than in
   sound.c, whose tables are shared with the FreeWili 2. */
#define TONE_HZ_CEILING  2500u

/* Speaker-safe peak. The FreeWili 2 caps amplitude in its codec path; this is
   the OG's equivalent, applied at synthesis. */
#define TONE_PEAK_MAX    12000

/* hz == 0 is a rest and passes through unclamped. */
uint16_t tone_clamp_hz(uint16_t hz);

/* Render a `ms`-long tone at `hz` with nominal loudness `amp` (0..255) into
   `out`, writing at most `max` samples and returning how many were written.
   hz == 0 renders silence of the same duration. Never writes past what it
   returns, and never past `max`. */
size_t tone_render(uint16_t hz, uint16_t ms, uint8_t amp, int16_t *out, size_t max);
#endif
