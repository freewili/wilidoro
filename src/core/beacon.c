#include "beacon.h"
#include <string.h>

uint16_t beacon_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

void beacon_pack(const beacon_msg_t *m, uint8_t out[BEACON_WIRE_LEN]) {
    out[0] = BEACON_MAGIC0;
    out[1] = BEACON_MAGIC1;
    out[2] = BEACON_VERSION;
    memcpy(&out[3], m->name, BEACON_NAME_LEN);   /* bytes 3..10 */
    out[11] = (uint8_t)m->state;
    out[12] = m->minutes_left;
    out[13] = m->completed;
    uint16_t crc = beacon_crc16(out, 14);
    out[14] = (uint8_t)(crc >> 8);
    out[15] = (uint8_t)(crc & 0xFF);
}

bool beacon_unpack(const uint8_t in[BEACON_WIRE_LEN], beacon_msg_t *out) {
    if (in[0] != BEACON_MAGIC0 || in[1] != BEACON_MAGIC1) return false;
    if (in[2] != BEACON_VERSION) return false;
    uint16_t crc = beacon_crc16(in, 14);
    if (in[14] != (uint8_t)(crc >> 8) || in[15] != (uint8_t)(crc & 0xFF)) return false;
    memcpy(out->name, &in[3], BEACON_NAME_LEN);
    out->state = (beacon_state_t)in[11];
    out->minutes_left = in[12];
    out->completed = in[13];
    return true;
}

/* --- OOK Manchester physical layer --- */
/* Build a half-bit level stream: preamble (Manchester 1s) then payload bits MSB-first.
   Manchester: bit 1 -> {high, low}; bit 0 -> {low, high}. */
static size_t build_halfbits(const uint8_t payload[BEACON_WIRE_LEN], uint8_t *hb) {
    size_t k = 0;
    for (int i = 0; i < BEACON_PREAMBLE_BITS; i++) { hb[k++] = 1; hb[k++] = 0; }
    for (int i = 0; i < BEACON_WIRE_LEN; i++) {
        for (int b = 7; b >= 0; b--) {
            uint8_t bit = (payload[i] >> b) & 1u;
            if (bit) { hb[k++] = 1; hb[k++] = 0; }
            else     { hb[k++] = 0; hb[k++] = 1; }
        }
    }
    return k;
}

size_t beacon_ook_encode(const uint8_t payload[BEACON_WIRE_LEN], uint32_t *durs, size_t max, bool *start_level) {
    uint8_t hb[(BEACON_PREAMBLE_BITS + BEACON_WIRE_LEN * 8) * 2];
    size_t nhb = build_halfbits(payload, hb);
    if (nhb == 0) return 0;
    *start_level = hb[0];
    size_t n = 0;
    uint32_t run = 0; uint8_t cur = hb[0];
    for (size_t i = 0; i < nhb; i++) {
        if (hb[i] == cur) { run++; }
        else {
            if (n >= max) return 0;
            durs[n++] = run * BEACON_HALFBIT_US;
            cur = hb[i]; run = 1;
        }
    }
    if (n >= max) return 0;
    durs[n++] = run * BEACON_HALFBIT_US;
    return n;
}

bool beacon_ook_decode(const uint32_t *durs, size_t n, uint8_t payload[BEACON_WIRE_LEN]) {
    /* Expand run-length durations back into a half-bit level stream (start level unknown:
       assume first run is level 1, i.e. preamble starts high). */
    uint8_t hb[(BEACON_PREAMBLE_BITS + BEACON_WIRE_LEN * 8) * 2 + 8];
    size_t nhb = 0; uint8_t level = 1;
    const uint32_t q = BEACON_HALFBIT_US;
    for (size_t i = 0; i < n; i++) {
        uint32_t units = (durs[i] + q / 2) / q;           /* nearest half-bit count */
        if (units == 0 || units > 4) return false;         /* implausible pulse */
        for (uint32_t u = 0; u < units && nhb < sizeof(hb); u++) hb[nhb++] = level;
        level ^= 1u;
    }
    size_t need = (BEACON_PREAMBLE_BITS + BEACON_WIRE_LEN * 8) * 2;
    if (nhb < need) return false;
    /* verify preamble */
    for (int i = 0; i < BEACON_PREAMBLE_BITS; i++)
        if (hb[i*2] != 1 || hb[i*2+1] != 0) return false;
    /* Manchester-decode payload */
    size_t off = BEACON_PREAMBLE_BITS * 2;
    for (int i = 0; i < BEACON_WIRE_LEN; i++) {
        uint8_t byte = 0;
        for (int b = 0; b < 8; b++) {
            uint8_t h0 = hb[off++], h1 = hb[off++];
            if (h0 == 1 && h1 == 0)      byte = (uint8_t)((byte << 1) | 1u);
            else if (h0 == 0 && h1 == 1) byte = (uint8_t)(byte << 1);
            else return false;
        }
        payload[i] = byte;
    }
    return true;
}
