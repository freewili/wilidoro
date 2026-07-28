#include "tone_synth.h"
#include <math.h>

uint16_t tone_clamp_hz(uint16_t hz) {
    if (hz == 0u) return 0u;                       /* a rest stays a rest */
    return hz > TONE_HZ_CEILING ? (uint16_t)TONE_HZ_CEILING : hz;
}

size_t tone_render(uint16_t hz, uint16_t ms, uint8_t amp, int16_t *out, size_t max) {
    if (!out || ms == 0u) return 0u;

    size_t n = ((size_t)TONE_RATE_HZ * (size_t)ms) / 1000u;
    if (n > max) n = max;

    hz = tone_clamp_hz(hz);
    if (hz == 0u || amp == 0u) {                   /* rest, or silent */
        for (size_t i = 0; i < n; i++) out[i] = 0;
        return n;
    }

    /* A sine, not a square: at 8 kHz a square's harmonics fold straight back
       into the audible band. Float, not double -- the RP2040 has no FPU and
       doubles are markedly slower in soft-float. ~3200 sinf calls for the
       longest note is a couple of milliseconds, once per note, before that
       note starts playing. */
    const float peak = ((float)amp / 255.0f) * (float)TONE_PEAK_MAX;
    const float step = 2.0f * 3.14159265f * (float)hz / (float)TONE_RATE_HZ;
    for (size_t i = 0; i < n; i++) {
        out[i] = (int16_t)(peak * sinf(step * (float)i));
    }
    return n;
}
