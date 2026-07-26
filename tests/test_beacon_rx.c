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

TEST clean_tail_decodes_on_the_first_attempt(void) {
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
    RUN_TEST(clean_tail_decodes_on_the_first_attempt);
    RUN_TEST(durations_before_the_first_gap_are_discarded);
    RUN_TEST(two_frames_back_to_back_both_decode);
    RUN_TEST(garbage_between_frames_does_not_block_the_second);
    RUN_TEST(overflow_discards_and_recovers);
    GREATEST_MAIN_END();
}
