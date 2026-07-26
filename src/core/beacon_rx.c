#include "beacon_rx.h"

void beacon_rx_init(beacon_rx_t *r) {
    beacon_rx_t z = {0};
    *r = z;
}

/* Try the segment as captured, then again with one synthetic low half-bit
   appended. ook_tx_send leaves the line low at the end of a burst, so when the
   last data bit is 1 (Manchester {high,low}) the frame's final low half-bit
   merges into the idle gap and never appears in the capture. Run-count parity
   cannot tell that apart from a frame that legitimately ends high, so both
   spellings get one attempt. */
static bool try_decode(beacon_rx_t *r, uint8_t out[BEACON_WIRE_LEN]) {
    if (r->n == 0) return false;
    if (beacon_ook_decode(r->durs, r->n, out)) return true;
    if (r->n >= BEACON_MAX_DURS) return false;
    r->durs[r->n] = BEACON_HALFBIT_US;
    return beacon_ook_decode(r->durs, r->n + 1, out);
}

bool beacon_rx_push(beacon_rx_t *r, uint32_t dur_us, uint8_t out[BEACON_WIRE_LEN]) {
    if (dur_us >= BEACON_GAP_US) {
        /* Idle is carrier-off, i.e. low, so the run after a gap is high --
           exactly the polarity beacon_ook_decode assumes. beacon_rx_flush
           re-arms the same way, so this is just an explicit close. */
        return beacon_rx_flush(r, out);
    }
    if (!r->armed) return false;          /* polarity unknown until the first gap */
    if (r->n >= BEACON_MAX_DURS) {        /* noise: drop it and resync on the next gap */
        r->n = 0;
        r->armed = false;
        return false;
    }
    r->durs[r->n++] = dur_us;
    return false;
}

bool beacon_rx_flush(beacon_rx_t *r, uint8_t out[BEACON_WIRE_LEN]) {
    bool got = try_decode(r, out);
    r->n = 0;
    r->armed = true;
    return got;
}
