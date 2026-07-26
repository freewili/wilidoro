// src/core/beacon_rx.h -- cut a raw OOK edge stream into decodable beacon frames.
#ifndef WILIDORO_BEACON_RX_H
#define WILIDORO_BEACON_RX_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "beacon.h"

/* beacon_ook_decode rejects any run longer than 4 half-bits, so 5 is the
   smallest unambiguous "this is the inter-frame gap" threshold. */
#define BEACON_GAP_US (BEACON_HALFBIT_US * 5u)

typedef struct {
    uint32_t durs[BEACON_MAX_DURS];
    size_t   n;
    bool     armed;   /* a gap has been seen, so the next run is known to be high */
} beacon_rx_t;

void beacon_rx_init(beacon_rx_t *r);

/* Feed one captured duration (microseconds the line held its current level).
   Returns true exactly when a frame decoded, writing the wire frame to `out`. */
bool beacon_rx_push(beacon_rx_t *r, uint32_t dur_us, uint8_t out[BEACON_WIRE_LEN]);
#endif
