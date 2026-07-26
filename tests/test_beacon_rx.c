#include "greatest.h"
#include "beacon_rx.h"
#include <string.h>

static void make_wire(uint8_t wire[BEACON_WIRE_LEN], const char *name8,
                      beacon_state_t st, uint8_t mins, uint8_t done) {
    beacon_msg_t m;
    memcpy(m.name, name8, BEACON_NAME_LEN);
    m.state = st; m.minutes_left = mins; m.completed = done;
    beacon_pack(&m, wire);
}

/* Push a run of durations; true if any single push reported a decoded frame. */
static bool push_all(beacon_rx_t *r, const uint32_t *durs, size_t n,
                     uint8_t out[BEACON_WIRE_LEN]) {
    bool got = false;
    for (size_t i = 0; i < n; i++) if (beacon_rx_push(r, durs[i], out)) got = true;
    return got;
}

/* A gap comfortably longer than the threshold, as a real idle period would be. */
#define GAP (BEACON_GAP_US * 4u)

/* Model gdo_capture.pio's actual emission rule (see
   wilibsp/bsp/radio/gdo_capture.pio and src/hal/hal_target.c's F1 comments),
   NOT the encoder's own output: a word is emitted only when a run ENDS, i.e.
   at the next level transition, and the stream strictly alternates starting
   from a low run (`pre_low`, the idle before the first rising edge).
   ook_tx_send always finishes a burst with gpio_put(0) (carrier off). Whether
   that produces a transition -- and therefore a final emitted word -- depends
   on parity: durs[] alternates starting high, so the last run is high iff `n`
   is odd. When `n` is even the last run is already low, gpio_put(0) is a
   no-op transition-wise, and that trailing low run is NEVER captured: no gap,
   no closing edge, nothing. This is the real hardware behaviour that the
   framer must cope with by flushing, not by waiting for a run that never
   comes. */
static size_t capture_of_burst(const uint32_t *durs, size_t n, uint32_t pre_low,
                                uint32_t *ring) {
    size_t k = 0;
    ring[k++] = pre_low;
    size_t emitted = (n % 2 == 1) ? n : n - 1;   /* last run high iff n is odd */
    for (size_t i = 0; i < emitted; i++) ring[k++] = durs[i];
    return k;
}

TEST round_trip_through_the_framer(void) {
    uint8_t wire[BEACON_WIRE_LEN]; make_wire(wire, "ALEX    ", BST_FOCUS, 17, 2);
    uint32_t durs[BEACON_MAX_DURS]; bool lvl = false;
    size_t n = beacon_ook_encode(wire, durs, BEACON_MAX_DURS, &lvl);
    ASSERT(n > 0);
    ASSERT(lvl);                       /* the preamble starts high */

    beacon_rx_t r; beacon_rx_init(&r);
    uint8_t got[BEACON_WIRE_LEN];
    ASSERT_FALSE(beacon_rx_push(&r, GAP, got));   /* leading gap arms the framer */
    ASSERT_FALSE(push_all(&r, durs, n, got));     /* nothing decodes mid-burst */
    ASSERT(beacon_rx_push(&r, GAP, got));         /* the trailing gap closes it */
    ASSERT_MEM_EQ(wire, got, BEACON_WIRE_LEN);
    PASS();
}

TEST merged_trailing_halfbit_still_decodes(void) {
    /* Find a frame whose final data bit is 1. Manchester encodes 1 as {high,low},
       so its trailing low half-bit is exactly what ook_tx_send merges into the
       idle gap -- it never reaches the capture. The final data bit is the LSB of
       the CRC low byte, the last wire byte. */
    uint8_t wire[BEACON_WIRE_LEN];
    int done = -1;
    for (int c = 0; c < 256; c++) {
        make_wire(wire, "ALEX    ", BST_FOCUS, 17, (uint8_t)c);
        if (wire[BEACON_WIRE_LEN - 1] & 1u) { done = c; break; }
    }
    ASSERT(done >= 0);
    make_wire(wire, "ALEX    ", BST_FOCUS, 17, (uint8_t)done);

    uint32_t durs[BEACON_MAX_DURS]; bool lvl = false;
    size_t n = beacon_ook_encode(wire, durs, BEACON_MAX_DURS, &lvl);
    ASSERT(n > 1);

    beacon_rx_t r; beacon_rx_init(&r);
    uint8_t got[BEACON_WIRE_LEN];
    beacon_rx_push(&r, GAP, got);
    push_all(&r, durs, n - 1, got);               /* drop the merged final low run */
    ASSERT(beacon_rx_push(&r, GAP, got));
    ASSERT_MEM_EQ(wire, got, BEACON_WIRE_LEN);
    PASS();
}

TEST clean_tail_decodes(void) {
    /* The mirror case: a final data bit of 0 is {low,high}, so the frame ends
       high, the gap terminates it, and the capture is already complete. */
    uint8_t wire[BEACON_WIRE_LEN];
    int done = -1;
    for (int c = 0; c < 256; c++) {
        make_wire(wire, "ALEX    ", BST_FOCUS, 17, (uint8_t)c);
        if ((wire[BEACON_WIRE_LEN - 1] & 1u) == 0u) { done = c; break; }
    }
    ASSERT(done >= 0);
    make_wire(wire, "ALEX    ", BST_FOCUS, 17, (uint8_t)done);

    uint32_t durs[BEACON_MAX_DURS]; bool lvl = false;
    size_t n = beacon_ook_encode(wire, durs, BEACON_MAX_DURS, &lvl);

    beacon_rx_t r; beacon_rx_init(&r);
    uint8_t got[BEACON_WIRE_LEN];
    beacon_rx_push(&r, GAP, got);
    push_all(&r, durs, n, got);
    ASSERT(beacon_rx_push(&r, GAP, got));
    ASSERT_MEM_EQ(wire, got, BEACON_WIRE_LEN);
    PASS();
}

TEST durations_before_the_first_gap_are_discarded(void) {
    uint8_t wire[BEACON_WIRE_LEN]; make_wire(wire, "ALEX    ", BST_FOCUS, 17, 2);
    uint32_t durs[BEACON_MAX_DURS]; bool lvl = false;
    size_t n = beacon_ook_encode(wire, durs, BEACON_MAX_DURS, &lvl);

    beacon_rx_t r; beacon_rx_init(&r);
    uint8_t got[BEACON_WIRE_LEN];
    /* No leading gap: polarity is unknown, so nothing may accumulate. */
    ASSERT_FALSE(push_all(&r, durs, n, got));
    ASSERT_FALSE(beacon_rx_push(&r, GAP, got));   /* empty segment, no decode */
    /* Now armed, the very next clean burst decodes. */
    ASSERT_FALSE(push_all(&r, durs, n, got));
    ASSERT(beacon_rx_push(&r, GAP, got));
    ASSERT_MEM_EQ(wire, got, BEACON_WIRE_LEN);
    PASS();
}

TEST two_frames_back_to_back_both_decode(void) {
    uint8_t w1[BEACON_WIRE_LEN], w2[BEACON_WIRE_LEN];
    make_wire(w1, "ALEX    ", BST_FOCUS, 17, 2);
    make_wire(w2, "BEA     ", BST_BREAK, 4, 9);
    uint32_t d1[BEACON_MAX_DURS], d2[BEACON_MAX_DURS]; bool lvl = false;
    size_t n1 = beacon_ook_encode(w1, d1, BEACON_MAX_DURS, &lvl);
    size_t n2 = beacon_ook_encode(w2, d2, BEACON_MAX_DURS, &lvl);

    beacon_rx_t r; beacon_rx_init(&r);
    uint8_t got[BEACON_WIRE_LEN];
    beacon_rx_push(&r, GAP, got);
    push_all(&r, d1, n1, got);
    ASSERT(beacon_rx_push(&r, GAP, got));
    ASSERT_MEM_EQ(w1, got, BEACON_WIRE_LEN);
    push_all(&r, d2, n2, got);
    ASSERT(beacon_rx_push(&r, GAP, got));
    ASSERT_MEM_EQ(w2, got, BEACON_WIRE_LEN);
    PASS();
}

TEST garbage_between_frames_does_not_block_the_second(void) {
    uint8_t w1[BEACON_WIRE_LEN], w2[BEACON_WIRE_LEN];
    make_wire(w1, "ALEX    ", BST_FOCUS, 17, 2);
    make_wire(w2, "BEA     ", BST_BREAK, 4, 9);
    uint32_t d1[BEACON_MAX_DURS], d2[BEACON_MAX_DURS]; bool lvl = false;
    size_t n1 = beacon_ook_encode(w1, d1, BEACON_MAX_DURS, &lvl);
    size_t n2 = beacon_ook_encode(w2, d2, BEACON_MAX_DURS, &lvl);

    beacon_rx_t r; beacon_rx_init(&r);
    uint8_t got[BEACON_WIRE_LEN];
    beacon_rx_push(&r, GAP, got);
    push_all(&r, d1, n1, got);
    ASSERT(beacon_rx_push(&r, GAP, got));
    ASSERT_MEM_EQ(w1, got, BEACON_WIRE_LEN);

    /* A short burst of nonsense, closed by its own gap: too few half-bits. */
    for (int i = 0; i < 9; i++) beacon_rx_push(&r, BEACON_HALFBIT_US * 3u, got);
    ASSERT_FALSE(beacon_rx_push(&r, GAP, got));

    push_all(&r, d2, n2, got);
    ASSERT(beacon_rx_push(&r, GAP, got));
    ASSERT_MEM_EQ(w2, got, BEACON_WIRE_LEN);
    PASS();
}

/* Find a `completed` byte whose encoded run count has the requested parity
   (odd != 0 for odd, 0 for even); capture_of_burst's dropped-final-run
   behaviour differs between the two, so the pinning test below covers both. */
static int find_wire_with_parity(int want_odd) {
    for (int c = 0; c < 256; c++) {
        uint8_t wire[BEACON_WIRE_LEN]; make_wire(wire, "ALEX    ", BST_FOCUS, 17, (uint8_t)c);
        uint32_t durs[BEACON_MAX_DURS]; bool lvl = false;
        size_t n = beacon_ook_encode(wire, durs, BEACON_MAX_DURS, &lvl);
        if (((int)(n % 2)) == (want_odd ? 1 : 0)) return c;
    }
    return -1;
}

/* This is the test that pins F1: beacon_rx_push alone must NEVER decode a
   hardware-shaped capture (no synthetic closing gap ever appears in it), and
   beacon_rx_flush must decode that exact same stream. No ASSERT* here -- this
   helper is not a TEST, so it hands results back for the callers to assert.

   *preamble_starts_high hands back beacon_ook_encode's own `start_level`, so
   the caller can assert it. capture_of_burst's "last run high iff n is odd"
   rule is only true because the preamble always starts high (build_halfbits
   always emits {1,0} first) -- silently baking that assumption in here would
   let a future change to the preamble polarity quietly pin the wrong parity
   instead of failing loudly. */
static void modelled_capture_case(uint8_t completed, bool *no_flush_decodes,
                                   bool *flush_decodes, bool *flush_correct,
                                   bool *preamble_starts_high) {
    uint8_t wire[BEACON_WIRE_LEN]; make_wire(wire, "ALEX    ", BST_FOCUS, 17, completed);
    uint32_t durs[BEACON_MAX_DURS]; bool lvl = false;
    size_t n = beacon_ook_encode(wire, durs, BEACON_MAX_DURS, &lvl);
    *preamble_starts_high = lvl;

    uint32_t ring[BEACON_MAX_DURS + 8];
    size_t rn = capture_of_burst(durs, n, 500000u, ring);

    /* Without a flush: exactly what the hardware would produce, and nothing
       ever decodes -- there is no closing gap in this stream at all. */
    beacon_rx_t r1; beacon_rx_init(&r1);
    uint8_t got1[BEACON_WIRE_LEN];
    *no_flush_decodes = push_all(&r1, ring, rn, got1);

    /* With a flush standing in for hal_beacon_rx's "the poll drained zero
       edges" close: the same modelled stream decodes correctly. */
    beacon_rx_t r2; beacon_rx_init(&r2);
    uint8_t got2[BEACON_WIRE_LEN];
    push_all(&r2, ring, rn, got2);
    *flush_decodes = beacon_rx_flush(&r2, got2);
    *flush_correct = *flush_decodes && memcmp(wire, got2, BEACON_WIRE_LEN) == 0;
}

TEST modelled_capture_decodes_only_with_a_flush_odd_run_count(void) {
    int done = find_wire_with_parity(1);
    ASSERT(done >= 0);
    bool no_flush = true, flush = false, correct = false, starts_high = false;
    modelled_capture_case((uint8_t)done, &no_flush, &flush, &correct, &starts_high);
    ASSERT(starts_high);   /* capture_of_burst's parity rule assumes this */
    ASSERT_FALSE(no_flush);
    ASSERT(flush);
    ASSERT(correct);
    PASS();
}

TEST modelled_capture_decodes_only_with_a_flush_even_run_count(void) {
    int done = find_wire_with_parity(0);
    ASSERT(done >= 0);
    bool no_flush = true, flush = false, correct = false, starts_high = false;
    modelled_capture_case((uint8_t)done, &no_flush, &flush, &correct, &starts_high);
    ASSERT(starts_high);   /* capture_of_burst's parity rule assumes this */
    ASSERT_FALSE(no_flush);
    ASSERT(flush);
    ASSERT(correct);
    PASS();
}

TEST overflow_discards_and_recovers(void) {
    beacon_rx_t r; beacon_rx_init(&r);
    uint8_t got[BEACON_WIRE_LEN];
    beacon_rx_push(&r, GAP, got);
    /* Far more runs than a frame can hold: the segment must be dropped, and the
       framer must not wedge. */
    for (size_t i = 0; i < BEACON_MAX_DURS + 10u; i++)
        ASSERT_FALSE(beacon_rx_push(&r, BEACON_HALFBIT_US, got));

    uint8_t wire[BEACON_WIRE_LEN]; make_wire(wire, "BEA     ", BST_BREAK, 4, 9);
    uint32_t durs[BEACON_MAX_DURS]; bool lvl = false;
    size_t n = beacon_ook_encode(wire, durs, BEACON_MAX_DURS, &lvl);
    ASSERT_FALSE(beacon_rx_push(&r, GAP, got));   /* re-arms after the overflow */
    ASSERT_FALSE(push_all(&r, durs, n, got));
    ASSERT(beacon_rx_push(&r, GAP, got));
    ASSERT_MEM_EQ(wire, got, BEACON_WIRE_LEN);
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(round_trip_through_the_framer);
    RUN_TEST(merged_trailing_halfbit_still_decodes);
    RUN_TEST(clean_tail_decodes);
    RUN_TEST(durations_before_the_first_gap_are_discarded);
    RUN_TEST(two_frames_back_to_back_both_decode);
    RUN_TEST(garbage_between_frames_does_not_block_the_second);
    RUN_TEST(modelled_capture_decodes_only_with_a_flush_odd_run_count);
    RUN_TEST(modelled_capture_decodes_only_with_a_flush_even_run_count);
    RUN_TEST(overflow_discards_and_recovers);
    GREATEST_MAIN_END();
}
