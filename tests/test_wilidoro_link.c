#include "greatest.h"
#include "wilidoro_link.h"
#include "beacon.h"
#include "common/link/link_frame.h"
#include <string.h>

static void sample_wire(uint8_t out[BEACON_WIRE_LEN]) {
    beacon_msg_t m;
    memcpy(m.name, "DAVE    ", BEACON_NAME_LEN);
    m.state = BST_FOCUS; m.minutes_left = 18; m.completed = 2;
    beacon_pack(&m, out);
}

/* Encode a payload, feed the bytes in ONE AT A TIME through the BSP's real
   decoder, and hand back what came out. This is the genuine round trip, not a
   mock: link_frame.c and crc.c are pure C and are linked into this binary. */
static bool round_trip(const void *payload, size_t len,
                       uint8_t *out_buf, size_t *out_len) {
    static uint8_t framed[FWOG_LINK_MAX_PAYLOAD + FWOG_LINK_OVERHEAD];
    const size_t n = fwog_link_encode(framed, sizeof framed, payload, len);
    if (n == 0) return false;
    static fwog_link_rx_t rx;
    fwog_link_rx_init(&rx);
    for (size_t i = 0; i < n; i++) {
        size_t got = 0;
        if (fwog_link_rx_byte(&rx, framed[i], &got)) {
            memcpy(out_buf, rx.buf, got);
            *out_len = got;
            return true;
        }
    }
    return false;
}

TEST beacon_tx_round_trips(void) {
    uint8_t wire[BEACON_WIRE_LEN]; sample_wire(wire);
    uint8_t msg[sizeof(wd_link_beacon_t)];
    const size_t n = wd_link_build_beacon(msg, sizeof msg, WD_LINK_MSG_BEACON_TX, wire);
    ASSERT_EQ(sizeof(wd_link_beacon_t), n);

    uint8_t got[64]; size_t got_len = 0;
    ASSERT(round_trip(msg, n, got, &got_len));
    ASSERT_EQ(WD_LINK_MSG_BEACON_TX, wd_link_type(got, got_len));

    uint8_t back[BEACON_WIRE_LEN];
    ASSERT(wd_link_parse_beacon(got, got_len, back));
    ASSERT_EQ(0, memcmp(wire, back, BEACON_WIRE_LEN));
    PASS();
}

TEST beacon_rx_round_trips(void) {
    uint8_t wire[BEACON_WIRE_LEN]; sample_wire(wire);
    uint8_t msg[sizeof(wd_link_beacon_t)];
    const size_t n = wd_link_build_beacon(msg, sizeof msg, WD_LINK_MSG_BEACON_RX, wire);
    ASSERT_EQ(sizeof(wd_link_beacon_t), n);
    uint8_t got[64]; size_t got_len = 0;
    ASSERT(round_trip(msg, n, got, &got_len));
    ASSERT_EQ(WD_LINK_MSG_BEACON_RX, wd_link_type(got, got_len));
    PASS();
}

TEST status_round_trips(void) {
    uint8_t msg[sizeof(wd_link_status_t)];
    const size_t n = wd_link_build_status(msg, sizeof msg,
                                          WD_STATUS_CS0_UP | WD_STATUS_CS1_UP,
                                          (uint8_t)WD_SELFTEST_PASS, -42, 0xA5u);
    ASSERT_EQ(sizeof(wd_link_status_t), n);

    uint8_t got[64]; size_t got_len = 0;
    ASSERT(round_trip(msg, n, got, &got_len));
    ASSERT_EQ(WD_LINK_MSG_STATUS, wd_link_type(got, got_len));

    wd_link_status_t st;
    ASSERT(wd_link_parse_status(got, got_len, &st));
    ASSERT_EQ(WD_STATUS_CS0_UP | WD_STATUS_CS1_UP, st.flags);
    ASSERT_EQ((uint8_t)WD_SELFTEST_PASS, st.selftest);
    ASSERT_EQ(-42, st.rssi_dbm);
    ASSERT_EQ(0xA5u, st.lqi);
    PASS();
}

TEST builders_reject_small_buffer(void) {
    uint8_t wire[BEACON_WIRE_LEN]; sample_wire(wire);
    uint8_t small[4] = {0xEE, 0xEE, 0xEE, 0xEE};
    ASSERT_EQ(0u, wd_link_build_beacon(small, sizeof small, WD_LINK_MSG_BEACON_TX, wire));
    ASSERT_EQ(0u, wd_link_build_status(small, sizeof small, 0u, 0u, 0, 0u));
    /* "writing nothing" is part of the contract, not just the return value. */
    ASSERT_EQ(0xEE, small[0]);
    PASS();
}

TEST builder_rejects_wrong_opcode(void) {
    uint8_t wire[BEACON_WIRE_LEN]; sample_wire(wire);
    uint8_t msg[sizeof(wd_link_beacon_t)];
    ASSERT_EQ(0u, wd_link_build_beacon(msg, sizeof msg, WD_LINK_MSG_STATUS, wire));
    PASS();
}

TEST corrupt_crc_never_decodes(void) {
    uint8_t wire[BEACON_WIRE_LEN]; sample_wire(wire);
    uint8_t msg[sizeof(wd_link_beacon_t)];
    const size_t n = wd_link_build_beacon(msg, sizeof msg, WD_LINK_MSG_BEACON_TX, wire);

    uint8_t framed[64];
    const size_t f = fwog_link_encode(framed, sizeof framed, msg, n);
    ASSERT(f > 0);
    framed[f - 1] ^= 0xFFu;   /* corrupt the CRC high byte */

    fwog_link_rx_t rx; fwog_link_rx_init(&rx);
    for (size_t i = 0; i < f; i++) {
        size_t got = 0;
        ASSERT_FALSE(fwog_link_rx_byte(&rx, framed[i], &got));
    }
    PASS();
}

TEST truncated_frame_never_decodes(void) {
    uint8_t wire[BEACON_WIRE_LEN]; sample_wire(wire);
    uint8_t msg[sizeof(wd_link_beacon_t)];
    const size_t n = wd_link_build_beacon(msg, sizeof msg, WD_LINK_MSG_BEACON_TX, wire);
    uint8_t framed[64];
    const size_t f = fwog_link_encode(framed, sizeof framed, msg, n);
    ASSERT(f > 2);

    fwog_link_rx_t rx; fwog_link_rx_init(&rx);
    for (size_t i = 0; i < f - 2u; i++) {   /* drop the last two bytes */
        size_t got = 0;
        ASSERT_FALSE(fwog_link_rx_byte(&rx, framed[i], &got));
    }
    PASS();
}

TEST foreign_and_short_payloads_are_ignored(void) {
    /* The BSP's own 0x21 SET_DIRS must not be misread as one of ours. */
    const uint8_t foreign[7] = {0x21u, 0, 0, 0, 0, 0, 0};
    ASSERT_EQ(0u, wd_link_type(foreign, sizeof foreign));

    /* Our opcode, but too short for its struct. */
    const uint8_t stub[3] = {WD_LINK_MSG_BEACON_RX, 0, 0};
    ASSERT_EQ(0u, wd_link_type(stub, sizeof stub));
    uint8_t back[BEACON_WIRE_LEN];
    ASSERT_FALSE(wd_link_parse_beacon(stub, sizeof stub, back));

    const uint8_t short_status[4] = {WD_LINK_MSG_STATUS, 0, 0, 0};
    ASSERT_EQ(0u, wd_link_type(short_status, sizeof short_status));
    wd_link_status_t st;
    ASSERT_FALSE(wd_link_parse_status(short_status, sizeof short_status, &st));

    ASSERT_EQ(0u, wd_link_type(NULL, 0));
    PASS();
}

TEST echo_matches_inside_window(void) {
    uint8_t wire[BEACON_WIRE_LEN]; sample_wire(wire);
    ASSERT(wd_link_is_echo(true, wire, 10000u, 10000u, wire));
    ASSERT(wd_link_is_echo(true, wire, 10000u, 10000u + WD_ECHO_WINDOW_MS, wire));
    PASS();
}

TEST echo_expires_after_window(void) {
    uint8_t wire[BEACON_WIRE_LEN]; sample_wire(wire);
    ASSERT_FALSE(wd_link_is_echo(true, wire, 10000u,
                                 10000u + WD_ECHO_WINDOW_MS + 1u, wire));
    PASS();
}

TEST echo_rejects_one_byte_difference(void) {
    uint8_t wire[BEACON_WIRE_LEN]; sample_wire(wire);
    uint8_t other[BEACON_WIRE_LEN]; memcpy(other, wire, BEACON_WIRE_LEN);
    other[BEACON_WIRE_LEN - 3] ^= 0x01u;   /* a different minutes_left */
    ASSERT_FALSE(wd_link_is_echo(true, wire, 10000u, 10000u, other));
    PASS();
}

TEST echo_false_before_first_transmit(void) {
    uint8_t wire[BEACON_WIRE_LEN]; sample_wire(wire);
    uint8_t never[BEACON_WIRE_LEN]; memset(never, 0, sizeof never);
    ASSERT_FALSE(wd_link_is_echo(false, never, 0u, 5000u, wire));
    PASS();
}

TEST echo_survives_millisecond_wraparound(void) {
    uint8_t wire[BEACON_WIRE_LEN]; sample_wire(wire);
    /* Transmitted 100 ms before the uint32 ms counter wrapped. */
    ASSERT(wd_link_is_echo(true, wire, 0xFFFFFF9Cu, 0x00000032u, wire));
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(beacon_tx_round_trips);
    RUN_TEST(beacon_rx_round_trips);
    RUN_TEST(status_round_trips);
    RUN_TEST(builders_reject_small_buffer);
    RUN_TEST(builder_rejects_wrong_opcode);
    RUN_TEST(corrupt_crc_never_decodes);
    RUN_TEST(truncated_frame_never_decodes);
    RUN_TEST(foreign_and_short_payloads_are_ignored);
    RUN_TEST(echo_matches_inside_window);
    RUN_TEST(echo_expires_after_window);
    RUN_TEST(echo_rejects_one_byte_difference);
    RUN_TEST(echo_false_before_first_transmit);
    RUN_TEST(echo_survives_millisecond_wraparound);
    GREATEST_MAIN_END();
}
