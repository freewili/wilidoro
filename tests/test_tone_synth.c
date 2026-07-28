#include "greatest.h"
#include "tone_synth.h"
#include <stdlib.h>

TEST clamps_above_ceiling(void) {
    ASSERT_EQ(TONE_HZ_CEILING, tone_clamp_hz(3000));
    ASSERT_EQ(TONE_HZ_CEILING, tone_clamp_hz(TONE_HZ_CEILING + 1));
    PASS();
}

TEST passes_through_below_ceiling(void) {
    ASSERT_EQ(440u,  tone_clamp_hz(440));
    ASSERT_EQ(TONE_HZ_CEILING, tone_clamp_hz(TONE_HZ_CEILING));
    PASS();
}

TEST rest_is_zero_hz_and_stays_zero(void) {
    ASSERT_EQ(0u, tone_clamp_hz(0));   /* a rest must not be clamped up */
    PASS();
}

TEST sample_count_follows_duration(void) {
    int16_t buf[TONE_MAX_SAMPLES];
    /* 8000 Hz * 40 ms = 320 samples */
    ASSERT_EQ(320u, tone_render(440, 40, 200, buf, TONE_MAX_SAMPLES));
    /* the documented maximum note */
    ASSERT_EQ(TONE_MAX_SAMPLES, tone_render(440, TONE_MAX_MS, 200, buf, TONE_MAX_SAMPLES));
    PASS();
}

TEST never_exceeds_the_buffer(void) {
    int16_t buf[64];
    /* Ask for far more than fits; it must truncate to `max`, not overrun. */
    const size_t n = tone_render(440, TONE_MAX_MS, 200, buf, 64);
    ASSERT(n <= 64u);
    PASS();
}

TEST does_not_write_past_what_it_returns(void) {
    enum { CAP = 512 };
    int16_t buf[CAP];
    for (int i = 0; i < CAP; i++) buf[i] = 0x5A5A;      /* sentinel */
    const size_t n = tone_render(440, 40, 200, buf, CAP);   /* 320 samples */
    for (size_t i = n; i < (size_t)CAP; i++) ASSERT_EQ((int16_t)0x5A5A, buf[i]);
    PASS();
}

TEST rest_renders_silence(void) {
    int16_t buf[TONE_MAX_SAMPLES];
    const size_t n = tone_render(0, 40, 200, buf, TONE_MAX_SAMPLES);
    ASSERT_EQ(320u, n);                       /* a rest still occupies time */
    for (size_t i = 0; i < n; i++) ASSERT_EQ(0, buf[i]);
    PASS();
}

TEST zero_amplitude_renders_silence(void) {
    int16_t buf[TONE_MAX_SAMPLES];
    const size_t n = tone_render(440, 40, 0, buf, TONE_MAX_SAMPLES);
    for (size_t i = 0; i < n; i++) ASSERT_EQ(0, buf[i]);
    PASS();
}

TEST louder_amplitude_gives_a_bigger_peak(void) {
    int16_t a[TONE_MAX_SAMPLES], b[TONE_MAX_SAMPLES];
    const size_t na = tone_render(440, 40, 80,  a, TONE_MAX_SAMPLES);
    const size_t nb = tone_render(440, 40, 240, b, TONE_MAX_SAMPLES);
    int pa = 0, pb = 0;
    for (size_t i = 0; i < na; i++) { const int v = abs(a[i]); if (v > pa) pa = v; }
    for (size_t i = 0; i < nb; i++) { const int v = abs(b[i]); if (v > pb) pb = v; }
    ASSERT(pb > pa);
    PASS();
}

TEST peak_respects_the_speaker_ceiling(void) {
    int16_t buf[TONE_MAX_SAMPLES];
    const size_t n = tone_render(440, 40, 255, buf, TONE_MAX_SAMPLES);
    int peak = 0;
    for (size_t i = 0; i < n; i++) { const int v = abs(buf[i]); if (v > peak) peak = v; }
    ASSERT(peak <= TONE_PEAK_MAX);
    ASSERT(peak > TONE_PEAK_MAX / 2);   /* full amplitude should approach it */
    PASS();
}

TEST zero_duration_renders_nothing(void) {
    int16_t buf[TONE_MAX_SAMPLES];
    ASSERT_EQ(0u, tone_render(440, 0, 200, buf, TONE_MAX_SAMPLES));
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(clamps_above_ceiling);
    RUN_TEST(passes_through_below_ceiling);
    RUN_TEST(rest_is_zero_hz_and_stays_zero);
    RUN_TEST(sample_count_follows_duration);
    RUN_TEST(never_exceeds_the_buffer);
    RUN_TEST(does_not_write_past_what_it_returns);
    RUN_TEST(rest_renders_silence);
    RUN_TEST(zero_amplitude_renders_silence);
    RUN_TEST(louder_amplitude_gives_a_bigger_peak);
    RUN_TEST(peak_respects_the_speaker_ceiling);
    RUN_TEST(zero_duration_renders_nothing);
    GREATEST_MAIN_END();
}
