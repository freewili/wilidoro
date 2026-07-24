#include "greatest.h"
#include "beacon.h"
#include <string.h>

static beacon_msg_t SAMPLE(void) {
    beacon_msg_t m; memcpy(m.name, "DAVE    ", 8);
    m.state = BST_FOCUS; m.minutes_left = 18; m.completed = 2;
    return m;
}

TEST crc_detects_single_bit_flip(void) {
    uint8_t buf[8] = {1,2,3,4,5,6,7,8};
    uint16_t a = beacon_crc16(buf, 8);
    buf[3] ^= 0x01;
    ASSERT(a != beacon_crc16(buf, 8));
    PASS();
}

TEST pack_unpack_roundtrip(void) {
    beacon_msg_t m = SAMPLE(), out;
    uint8_t wire[BEACON_WIRE_LEN];
    beacon_pack(&m, wire);
    ASSERT(beacon_unpack(wire, &out));
    ASSERT_EQ(0, memcmp(m.name, out.name, 8));
    ASSERT_EQ(BST_FOCUS, out.state);
    ASSERT_EQ(18, out.minutes_left);
    ASSERT_EQ(2, out.completed);
    PASS();
}

TEST unpack_rejects_bad_magic(void) {
    beacon_msg_t m = SAMPLE(), out;
    uint8_t wire[BEACON_WIRE_LEN];
    beacon_pack(&m, wire);
    wire[0] ^= 0xFF;
    ASSERT_FALSE(beacon_unpack(wire, &out));
    PASS();
}

TEST unpack_rejects_corrupt_crc(void) {
    beacon_msg_t m = SAMPLE(), out;
    uint8_t wire[BEACON_WIRE_LEN];
    beacon_pack(&m, wire);
    wire[9] ^= 0x20;   /* flip a payload bit; CRC must fail */
    ASSERT_FALSE(beacon_unpack(wire, &out));
    PASS();
}

TEST ook_roundtrip(void) {
    beacon_msg_t m = SAMPLE();
    uint8_t wire[BEACON_WIRE_LEN], back[BEACON_WIRE_LEN];
    beacon_pack(&m, wire);
    uint32_t durs[BEACON_MAX_DURS];
    bool start_level;
    size_t n = beacon_ook_encode(wire, durs, BEACON_MAX_DURS, &start_level);
    ASSERT(n > 0);
    ASSERT(beacon_ook_decode(durs, n, back));
    ASSERT_EQ(0, memcmp(wire, back, BEACON_WIRE_LEN));
    PASS();
}

TEST ook_decode_rejects_noise(void) {
    uint32_t noise[16] = { 137, 900, 42, 3100, 12, 77, 2200, 5, 640, 51, 990, 8, 33, 1200, 7, 410 };
    uint8_t back[BEACON_WIRE_LEN];
    ASSERT_FALSE(beacon_ook_decode(noise, 16, back));
    PASS();
}

GREATEST_MAIN_DEFS();
int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(crc_detects_single_bit_flip);
    RUN_TEST(pack_unpack_roundtrip);
    RUN_TEST(unpack_rejects_bad_magic);
    RUN_TEST(unpack_rejects_corrupt_crc);
    RUN_TEST(ook_roundtrip);
    RUN_TEST(ook_decode_rejects_noise);
    GREATEST_MAIN_END();
}
