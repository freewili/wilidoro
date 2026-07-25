#include "greatest.h"
#include "sound.h"

/* --- table integrity: every theme x every sound obeys the documented bounds --- */
TEST tables_are_well_formed(void) {
    for (uint8_t t = 0; t < SOUND_THEME_COUNT; t++) {
        for (int id = 0; id < SND_COUNT; id++) {
            uint8_t n = 0;
            const sound_note_t *s = sound_seq(t, (sound_id_t)id, &n);
            ASSERT(s != NULL);
            ASSERT(n >= 1 && n <= SOUND_MAX_NOTES);
            uint32_t total = 0;
            for (uint8_t k = 0; k < n; k++) {
                if (s[k].hz != 0) {
                    ASSERT(s[k].hz >= SOUND_HZ_MIN);
                    ASSERT(s[k].hz <= SOUND_HZ_MAX);
                }
                ASSERT(s[k].ms >= SOUND_MS_MIN);
                ASSERT(s[k].ms <= SOUND_MS_MAX);
                total += s[k].ms;
            }
            ASSERT(total <= SOUND_SEQ_MS_MAX);
        }
    }
    PASS();
}

TEST bad_id_returns_null(void) {
    uint8_t n = 99;
    ASSERT(sound_seq(0, SND_COUNT, &n) == NULL);
    PASS();
}

TEST out_of_range_theme_clamps_to_zero(void) {
    uint8_t n0 = 0, n7 = 0;
    const sound_note_t *a = sound_seq(0, SND_START, &n0);
    const sound_note_t *b = sound_seq(7, SND_START, &n7);
    ASSERT_EQ(a, b);
    ASSERT_EQ(n0, n7);
    PASS();
}

/* --- sequencing --- */
TEST first_note_dispatches_immediately(void) {
    sound_player_t p; sound_reset(&p);
    uint8_t n = 0; const sound_note_t *s = sound_seq(1, SND_START, &n);
    sound_play(&p, 1, SND_START, 100, 1000);
    sound_note_t out;
    ASSERT(sound_next(&p, 1000, &out));
    ASSERT_EQ(s[0].hz, out.hz);
    ASSERT_EQ(s[0].ms, out.ms);
    ASSERT_EQ(s[0].amp, out.amp);          /* volume 100 => unscaled */
    PASS();
}

TEST second_note_waits_for_the_first_to_finish(void) {
    sound_player_t p; sound_reset(&p);
    uint8_t n = 0; const sound_note_t *s = sound_seq(1, SND_START, &n);
    ASSERT(n >= 2);
    sound_play(&p, 1, SND_START, 100, 1000);
    sound_note_t out;
    ASSERT(sound_next(&p, 1000, &out));
    ASSERT_FALSE(sound_next(&p, 1000 + s[0].ms - 1, &out));
    ASSERT(sound_next(&p, 1000 + s[0].ms, &out));
    ASSERT_EQ(s[1].hz, out.hz);
    PASS();
}

TEST sequence_ends_after_the_last_note_tail(void) {
    sound_player_t p; sound_reset(&p);
    uint8_t n = 0; const sound_note_t *s = sound_seq(2, SND_BREAK_END, &n);
    sound_play(&p, 2, SND_BREAK_END, 100, 0);
    uint32_t t = 0; sound_note_t out;
    for (uint8_t k = 0; k < n; k++) {
        ASSERT(sound_next(&p, t, &out));
        t += s[k].ms;
    }
    ASSERT(sound_active(&p));              /* last note still sounding */
    ASSERT_FALSE(sound_next(&p, t, &out)); /* tail elapsed -> nothing more */
    ASSERT_FALSE(sound_active(&p));
    PASS();
}

/* --- volume --- */
TEST volume_scales_amplitude(void) {
    sound_player_t p; sound_reset(&p);
    uint8_t n = 0; const sound_note_t *s = sound_seq(0, SND_FOCUS_END, &n);
    sound_play(&p, 0, SND_FOCUS_END, 50, 0);
    sound_note_t out;
    ASSERT(sound_next(&p, 0, &out));
    ASSERT_EQ((uint8_t)((uint32_t)s[0].amp * 50u / 100u), out.amp);
    ASSERT_EQ(s[0].hz, out.hz);            /* pitch is unaffected by volume */
    PASS();
}

TEST volume_zero_plays_nothing(void) {
    sound_player_t p; sound_reset(&p);
    sound_play(&p, 0, SND_FOCUS_END, 0, 0);
    sound_note_t out;
    ASSERT_FALSE(sound_active(&p));
    ASSERT_FALSE(sound_next(&p, 0, &out));
    PASS();
}

/* --- pre-emption --- */
TEST a_new_sound_preempts_the_current_one(void) {
    sound_player_t p; sound_reset(&p);
    sound_note_t out;
    sound_play(&p, 1, SND_BLIP, 100, 0);
    ASSERT(sound_next(&p, 0, &out));
    uint8_t n = 0; const sound_note_t *alarm = sound_seq(1, SND_FOCUS_END, &n);
    sound_play(&p, 1, SND_FOCUS_END, 100, 10);
    ASSERT(sound_next(&p, 10, &out));      /* restarts at the alarm's note 0 */
    ASSERT_EQ(alarm[0].hz, out.hz);
    PASS();
}

/* --- rests --- */
TEST a_rest_is_dispatched_as_silence(void) {
    sound_player_t p; sound_reset(&p);
    uint8_t n = 0; const sound_note_t *s = sound_seq(2, SND_FOCUS_END, &n);
    int rests = 0;
    for (uint8_t k = 0; k < n; k++) if (s[k].hz == 0) rests++;
    ASSERT(rests > 0);                     /* the flip alarm is a double chime */
    sound_play(&p, 2, SND_FOCUS_END, 100, 0);
    uint32_t t = 0; sound_note_t out; int seen = 0;
    for (uint8_t k = 0; k < n; k++) {
        ASSERT(sound_next(&p, t, &out));
        if (out.hz == 0) { ASSERT_EQ(0, out.amp); seen++; }
        t += s[k].ms;
    }
    ASSERT_EQ(rests, seen);
    PASS();
}

TEST idle_player_yields_nothing(void) {
    sound_player_t p; sound_reset(&p);
    sound_note_t out;
    ASSERT_FALSE(sound_active(&p));
    ASSERT_FALSE(sound_next(&p, 12345, &out));
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(tables_are_well_formed);
    RUN_TEST(bad_id_returns_null);
    RUN_TEST(out_of_range_theme_clamps_to_zero);
    RUN_TEST(first_note_dispatches_immediately);
    RUN_TEST(second_note_waits_for_the_first_to_finish);
    RUN_TEST(sequence_ends_after_the_last_note_tail);
    RUN_TEST(volume_scales_amplitude);
    RUN_TEST(volume_zero_plays_nothing);
    RUN_TEST(a_new_sound_preempts_the_current_one);
    RUN_TEST(a_rest_is_dispatched_as_silence);
    RUN_TEST(idle_player_yields_nothing);
    GREATEST_MAIN_END();
}
