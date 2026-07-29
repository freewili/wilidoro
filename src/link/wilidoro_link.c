#include "wilidoro_link.h"
#include <string.h>

/* Every builder assembles into a local struct and memcpys it out, so `out`
   needs no alignment guarantee. */

size_t wd_link_build_beacon(void *out, size_t cap, uint8_t type,
                            const uint8_t wire[BEACON_WIRE_LEN]) {
    if (type != WD_LINK_MSG_BEACON_TX && type != WD_LINK_MSG_BEACON_RX) return 0u;
    if (out == NULL || cap < sizeof(wd_link_beacon_t)) return 0u;
    wd_link_beacon_t m;
    m.type = type;
    memcpy(m.wire, wire, BEACON_WIRE_LEN);
    memcpy(out, &m, sizeof m);
    return sizeof m;
}

size_t wd_link_build_status(void *out, size_t cap, uint8_t flags,
                            uint8_t selftest, int8_t rssi_dbm, uint8_t lqi) {
    if (out == NULL || cap < sizeof(wd_link_status_t)) return 0u;
    wd_link_status_t m;
    m.type = WD_LINK_MSG_STATUS;
    m.flags = flags;
    m.selftest = selftest;
    m.rssi_dbm = rssi_dbm;
    m.lqi = lqi;
    memcpy(out, &m, sizeof m);
    return sizeof m;
}

uint8_t wd_link_type(const void *payload, size_t len) {
    if (payload == NULL || len < 1u) return 0u;
    const uint8_t t = *(const uint8_t *)payload;
    switch (t) {
    case WD_LINK_MSG_BEACON_TX:
    case WD_LINK_MSG_BEACON_RX:
        return (len >= sizeof(wd_link_beacon_t)) ? t : 0u;
    case WD_LINK_MSG_STATUS:
        return (len >= sizeof(wd_link_status_t)) ? t : 0u;
    default:
        return 0u;
    }
}

bool wd_link_parse_beacon(const void *payload, size_t len,
                          uint8_t out[BEACON_WIRE_LEN]) {
    const uint8_t t = wd_link_type(payload, len);
    if (t != WD_LINK_MSG_BEACON_TX && t != WD_LINK_MSG_BEACON_RX) return false;
    wd_link_beacon_t m;
    memcpy(&m, payload, sizeof m);
    memcpy(out, m.wire, BEACON_WIRE_LEN);
    return true;
}

bool wd_link_parse_status(const void *payload, size_t len,
                          wd_link_status_t *out) {
    if (out == NULL) return false;
    if (wd_link_type(payload, len) != WD_LINK_MSG_STATUS) return false;
    memcpy(out, payload, sizeof *out);
    return true;
}

bool wd_link_is_echo(bool have_tx, const uint8_t last_tx[BEACON_WIRE_LEN],
                     uint32_t last_tx_ms, uint32_t now_ms,
                     const uint8_t frame[BEACON_WIRE_LEN]) {
    if (!have_tx) return false;
    /* Unsigned subtraction, so a wrapped millisecond counter still yields the
       true elapsed time. A clock that went BACKWARDS yields a huge value and
       falls out as "not an echo", which is the safe direction: the frame is
       shown rather than silently swallowed. */
    if ((uint32_t)(now_ms - last_tx_ms) > WD_ECHO_WINDOW_MS) return false;
    return memcmp(last_tx, frame, BEACON_WIRE_LEN) == 0;
}
