// src/core/beacon_rx.h -- cut a raw OOK edge stream into decodable beacon frames.
#ifndef WILIDORO_BEACON_RX_H
#define WILIDORO_BEACON_RX_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "beacon.h"

/* beacon_ook_decode rejects a run once its rounded half-bit count exceeds 4,
   i.e. at dur_us >= 2250 (after its own +q/2 rounding) -- so with a 2500 us
   threshold here, runs of 2250..2499 us were neither recognised as the
   inter-frame gap nor legal as data: they got pushed into the segment and
   silently destroyed it (decode outright fails) instead of closing it. 4.5
   half-bits is the smallest threshold that is both unambiguous as a gap and
   at or below decode's own rejection point. */
#define BEACON_GAP_US (BEACON_HALFBIT_US * 9u / 2u)

typedef struct {
    uint32_t durs[BEACON_MAX_DURS];
    size_t   n;
    bool     armed;   /* a gap has been seen, so the next run is known to be high */
} beacon_rx_t;

void beacon_rx_init(beacon_rx_t *r);

/* Feed one captured duration (microseconds the line held its current level).
   Returns true exactly when a frame decoded, writing the wire frame to `out`. */
bool beacon_rx_push(beacon_rx_t *r, uint32_t dur_us, uint8_t out[BEACON_WIRE_LEN]);

/* Close the current segment now, without waiting for a gap run. The capture
   hardware timestamps a run only when it ENDS, so a burst's trailing low never
   arrives until some later edge -- the caller flushes when it can prove the line
   has gone quiet. */
bool beacon_rx_flush(beacon_rx_t *r, uint8_t out[BEACON_WIRE_LEN]);
#endif
