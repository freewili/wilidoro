/* Wilidoro's own messages on the FreeWili OG inter-CPU link.
 *
 * Compiled into BOTH OG executables and into the host test. That is the
 * point: an encoding crossing a link between two separately-flashed CPUs
 * must have exactly one definition.
 *
 * These ride the BSP's framing unchanged (link_frame.h: SOF 0x7E, len16,
 * CRC-16/XMODEM). Byte 0 of every payload is the message type -- 0x00 is
 * invalid, 0x01-0x1F are frozen for the display bootloader, and 0x20-0x22
 * are the BSP's own breakout-I/O messages, so ours start at 0x40 and leave
 * 0x23-0x3F free for the BSP.
 *
 * Structs go on the wire unserialized, like io_proto.h's: both ends are
 * little-endian Cortex-M0+ and every struct is __attribute__((packed)), so
 * the compiler emits byte-wise access and there is no endianness step. The
 * _Static_asserts are what make that safe rather than merely true today.
 *
 * Nothing here includes an SDK header, which is what lets the host test link
 * it directly against the BSP's real link_frame.c. */
#ifndef WILIDORO_LINK_H
#define WILIDORO_LINK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "beacon.h"   /* BEACON_WIRE_LEN */

#define WD_LINK_MSG_BEACON_TX 0x40u   /* display -> main: transmit this frame */
#define WD_LINK_MSG_BEACON_RX 0x41u   /* main -> display: heard off the air */
#define WD_LINK_MSG_STATUS    0x42u   /* main -> display: radio health */

typedef struct __attribute__((packed)) {
    uint8_t type;                     /* 0x40 or 0x41 */
    uint8_t wire[BEACON_WIRE_LEN];    /* a beacon_pack() frame */
} wd_link_beacon_t;
_Static_assert(sizeof(wd_link_beacon_t) == 17,
               "wd_link_beacon_t goes on the wire unserialized");

/* NO_KEY and NO_RX are deliberately separate. bench_main records that
 * collapsing "the transmitter never keyed" into "nothing was received" is
 * what made its 315 MHz result undiagnosable on first measurement. */
typedef enum {
    WD_SELFTEST_NOT_RUN = 0,
    WD_SELFTEST_PASS    = 1,   /* keyed, heard, payload matched */
    WD_SELFTEST_NO_KEY  = 2,   /* GDO0 never showed sync/end: TX did not key */
    WD_SELFTEST_NO_RX   = 3,   /* keyed, but the RF path carried nothing */
    WD_SELFTEST_CORRUPT = 4,   /* arrived, payload did not match */
} wd_selftest_t;

#define WD_STATUS_CS0_UP 0x01u
#define WD_STATUS_CS1_UP 0x02u

typedef struct __attribute__((packed)) {
    uint8_t type;        /* 0x42 */
    uint8_t flags;       /* WD_STATUS_CS0_UP | WD_STATUS_CS1_UP */
    uint8_t selftest;    /* wd_selftest_t */
    int8_t  rssi_dbm;    /* self-test packet RSSI; 0 when not run */
    uint8_t lqi;         /* RAW LQI register: bit7 = CRC_OK, bits 6:0 = estimate */
} wd_link_status_t;
_Static_assert(sizeof(wd_link_status_t) == 5,
               "wd_link_status_t goes on the wire unserialized");

/* Builders. Each returns bytes written, or 0 if `cap` is too small (writing
   nothing) or `type` is not a beacon opcode. `out` needs no alignment: these
   build into a local struct and memcpy it out. */
size_t wd_link_build_beacon(void *out, size_t cap, uint8_t type,
                            const uint8_t wire[BEACON_WIRE_LEN]);
size_t wd_link_build_status(void *out, size_t cap, uint8_t flags,
                            uint8_t selftest, int8_t rssi_dbm, uint8_t lqi);

/* The message type if `payload` is one of ours AND long enough for its own
   struct, else 0. Returning 0 is safe because the framing reserves it as
   invalid -- same contract as fwog_io_proto_type(). */
uint8_t wd_link_type(const void *payload, size_t len);

bool wd_link_parse_beacon(const void *payload, size_t len,
                          uint8_t out[BEACON_WIRE_LEN]);
bool wd_link_parse_status(const void *payload, size_t len,
                          wd_link_status_t *out);

/* How long after a transmit an identical frame is still assumed to be our own
   echo. The real round trip is sub-millisecond, so this is generous by three
   orders of magnitude; it exists to bound the damage if a genuine neighbour is
   ever byte-identical to us, which requires it to share our name, state,
   minutes remaining AND completed count. */
#define WD_ECHO_WINDOW_MS 1000u

/* Is `frame` our own transmission coming back? The OG's receiver sits
   centimetres from its transmitter, so it hears everything it sends.
   `have_tx` is false before the first transmit, when last_tx/last_tx_ms carry
   no meaning. Pure, so it is host-tested -- the same #ifndef HOST_TEST split
   cc1101.c and lis3dh.c already use. */
bool wd_link_is_echo(bool have_tx, const uint8_t last_tx[BEACON_WIRE_LEN],
                     uint32_t last_tx_ms, uint32_t now_ms,
                     const uint8_t frame[BEACON_WIRE_LEN]);

#endif
