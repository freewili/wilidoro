// src/core/beacon.h
#ifndef WILIDORO_BEACON_H
#define WILIDORO_BEACON_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define BEACON_MAGIC0 0x57   /* 'W' */
#define BEACON_MAGIC1 0x44   /* 'D' */
#define BEACON_VERSION 1
#define BEACON_NAME_LEN 8
#define BEACON_WIRE_LEN 16   /* magic2 ver1 name8 state1 min1 done1 crc16 */
#define BEACON_HALFBIT_US 500u
#define BEACON_PREAMBLE_BITS 8
#define BEACON_MAX_DURS (BEACON_PREAMBLE_BITS*2 + BEACON_WIRE_LEN*8*2 + 4)

typedef enum { BST_IDLE = 0, BST_FOCUS = 1, BST_BREAK = 2 } beacon_state_t;
typedef struct {
    char           name[BEACON_NAME_LEN];  /* space-padded ASCII, not NUL-terminated */
    beacon_state_t state;
    uint8_t        minutes_left;
    uint8_t        completed;
} beacon_msg_t;

uint16_t beacon_crc16(const uint8_t *data, size_t len);          /* CRC-16/CCITT-FALSE, init 0xFFFF, poly 0x1021 */
void     beacon_pack(const beacon_msg_t *m, uint8_t out[BEACON_WIRE_LEN]);
bool     beacon_unpack(const uint8_t in[BEACON_WIRE_LEN], beacon_msg_t *out);  /* false on magic/ver/crc mismatch */
size_t   beacon_ook_encode(const uint8_t payload[BEACON_WIRE_LEN], uint32_t *durs, size_t max, bool *start_level);
bool     beacon_ook_decode(const uint32_t *durs, size_t n, uint8_t payload[BEACON_WIRE_LEN]);
#endif
