# Plan OG-D — the sub-GHz beacon — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the FreeWili OG a working focus beacon — transmit pomodoro state over the CC1101, hear other devices, and populate the Nearby screen that has shown "no devices heard" on every board it has ever run on.

**Architecture:** The display CPU has no radio, so `hal_beacon_tx/rx` become messages over the inter-CPU UART link. Three opcodes (`0x40` TX, `0x41` RX, `0x42` radio status) ride the BSP's existing host-tested framing. The main CPU owns both CC1101s: `CS0` transmits, `CS1` sits permanently in RX. Nothing above the HAL changes except one new softkey.

**Tech Stack:** C11, Pico SDK (RP2040 ×2), `wiliOGbsp` (submodule), CMake + Ninja, CTest + `greatest.h` for host tests.

**Spec:** `docs/superpowers/specs/2026-07-29-wilidoro-OG-D-beacon-design.md` (commit `0c0ec4a`).

**Branch:** `og-d-beacon`, already created off `main`.

## Global Constraints

Every task's requirements implicitly include all of these. Violating any one is a build failure, a brick, or a silent corruption.

- **Never `printf`.** UART0 is the inter-CPU link on both CPUs. Use `DIAG()`.
- **Never pass a float or `%f` to `DIAG()`.**
- **Never add a watchdog to the display CPU.** The main CPU has one, armed by `board_init()` at 2 s; **every** main-CPU loop that can run longer than that must call `board_watchdog_kick()`.
- **Never flash the display app by UF2.** `fw flash wilidoro_main` is always correct; `fw flash wilidoro_display` takes the display CPU off USB permanently. Use `powershell -File tools/flash_og.ps1`.
- **`wiliOGbsp/` is read-only.** It is a submodule. No task here modifies it.
- **Do not touch the FW2 build.** `src/hal/hal_target.c`, `beacon_ook_encode`/`decode` and `cmake/board_fw2.cmake` stay as they are, with the single exception of the one-line no-op stub in Task 4 Step 3.
- **Frequency is 433.92 MHz only.** `WD_BEACON_HZ = 433920000u`. This is the one band needing no PCAL6416 expander traffic, because `fwog_ioexp_init()` already comes up at `FWOG_ANT_400MHZ` for both radios. No task here sends expander messages or calls `fwog_io_dir_apply()`.
- **Opcodes are `0x40`, `0x41`, `0x42`.** `0x00` is invalid, `0x01`–`0x1F` are frozen for the display bootloader, `0x20`–`0x22` are the BSP's own. Do not renumber.
- **Commit trailers:** this repo's commits end with `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>` and a `Claude-Session:` line. Match the existing log.

**Commands** (run from the repo root, `C:\~prj\Dropbox\vibeProjects\wilidoro`):

| | |
|---|---|
| Host tests | `powershell -File tools/test.ps1` |
| OG device build | `powershell -File tools/build_og.ps1` |
| FW2 device build | `powershell -File tools/build.ps1` |
| OG simulator | `powershell -File tools/sim.ps1 -Board og` |
| Flash the OG | `powershell -File tools/flash_og.ps1` |

## File Structure

| File | Responsibility |
|---|---|
| `src/link/wilidoro_link.h` (new) | Opcodes, the two packed wire structs, the self-test enum, and pure helper declarations. Included by **both** CPUs and the host test — one definition, no drift. |
| `src/link/wilidoro_link.c` (new) | Build/parse the three messages, and the echo decision. No SDK dependency, so it compiles on the host. |
| `tests/test_wilidoro_link.c` (new) | Host tests, including a genuine encode→byte-feed→decode round trip through the BSP's real framing. |
| `tests/CMakeLists.txt` (modify) | Declare the new test binary. |
| `cmake/board_og.cmake` (modify) | Compile `src/link/` into both OG executables. |
| `src/main_og/main.c` (modify) | Main CPU: bring both CC1101s up, run the over-the-air self-test, relay `0x40`↔air↔`0x41`, emit `0x42` every 5 s. |
| `src/hal/hal.h` (modify) | Add the `hal_beacon_show_self()` seam. |
| `src/hal/hal_og.c` (modify) | Display CPU: link bring-up, real `hal_beacon_tx/rx`, echo drop, dynamic `hal_caps().radio`. |
| `src/hal/hal_target.c`, `src/hal/hal_sim.c` (modify) | One-line no-op stubs for the new seam. |
| `src/ui/screen_nearby.c` (modify) | The `Self` softkey and the accent tint on our own row. OG-only. |
| `docs/hardware-notes.md` (modify) | The OG on-device beacon checklist, and the two items that genuinely need a second board. |

**Tasks 2 and 3 have no automated test.** They are main-CPU device code against the SDK and cannot run on the host. Their gate is: both device builds compile, the whole host suite still passes, and the on-board checklist in Task 6 is executed by a human. This is stated plainly rather than papered over with a fake test.

---

### Task 1: The link module and its host tests

**Files:**
- Create: `src/link/wilidoro_link.h`
- Create: `src/link/wilidoro_link.c`
- Test: `tests/test_wilidoro_link.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `BEACON_WIRE_LEN` (16) and `BEACON_NAME_LEN` (8) from `src/core/beacon.h`.
- Produces: everything Tasks 2–4 build on — `WD_LINK_MSG_BEACON_TX` (`0x40`), `WD_LINK_MSG_BEACON_RX` (`0x41`), `WD_LINK_MSG_STATUS` (`0x42`); types `wd_link_beacon_t`, `wd_link_status_t`, `wd_selftest_t`; flags `WD_STATUS_CS0_UP`, `WD_STATUS_CS1_UP`; and functions `wd_link_build_beacon()`, `wd_link_build_status()`, `wd_link_type()`, `wd_link_parse_beacon()`, `wd_link_parse_status()`, `wd_link_is_echo()`.

- [ ] **Step 1: Write the header**

Create `src/link/wilidoro_link.h`:

```c
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
```

- [ ] **Step 2: Write the failing test**

Create `tests/test_wilidoro_link.c`:

```c
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
    uint8_t framed[FWOG_LINK_MAX_PAYLOAD + FWOG_LINK_OVERHEAD];
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
```

- [ ] **Step 3: Declare the test binary**

Append to `tests/CMakeLists.txt`. Note it compiles the BSP's **real** `link_frame.c` and `crc.c` — both are pure C with no SDK dependency, which is what makes the round trip genuine:

```cmake
# Plan OG-D. Links the BSP's REAL framing (link_frame.c + crc.c, both pure C
# with no SDK dependency) so the round trip is genuine rather than mocked.
set(LINK ${CMAKE_CURRENT_SOURCE_DIR}/../src/link)
set(BSP  ${CMAKE_CURRENT_SOURCE_DIR}/../wiliOGbsp/bsp)
add_executable(test_wilidoro_link test_wilidoro_link.c
    ${LINK}/wilidoro_link.c
    ${BSP}/common/link/link_frame.c
    ${BSP}/common/crc.c
    ${CORE}/beacon.c)
target_include_directories(test_wilidoro_link PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CORE} ${LINK} ${BSP})
target_compile_options(test_wilidoro_link PRIVATE -Wall -Wextra)
target_link_libraries(test_wilidoro_link m)
add_test(NAME test_wilidoro_link COMMAND test_wilidoro_link)
```

- [ ] **Step 4: Run the test to verify it fails**

Run: `powershell -File tools/test.ps1`
Expected: the build FAILS at link time with undefined references to `wd_link_build_beacon`, `wd_link_build_status`, `wd_link_type`, `wd_link_parse_beacon`, `wd_link_parse_status` and `wd_link_is_echo` — `wilidoro_link.c` does not exist yet. That is the correct failure; do not proceed until you have seen it.

- [ ] **Step 5: Write the implementation**

Create `src/link/wilidoro_link.c`:

```c
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
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `powershell -File tools/test.ps1`
Expected: PASS. **12** test binaries now (the existing 11 plus `test_wilidoro_link`), all green, ~1 second. If `_Static_assert` fires on a struct size, the `__attribute__((packed))` was dropped — fix that, do not relax the assert.

- [ ] **Step 7: Commit**

```bash
git add src/link tests/test_wilidoro_link.c tests/CMakeLists.txt
git commit -m "feat(og): the OG-D link module -- 0x40/0x41/0x42 and the echo test"
```

---

### Task 2: Main CPU — bring both CC1101s up and self-test over the air

**Files:**
- Modify: `src/main_og/main.c`
- Modify: `cmake/board_og.cmake`

**Interfaces:**
- Consumes: from Task 1 — `wd_selftest_t` and its five values, `WD_STATUS_CS0_UP`, `WD_STATUS_CS1_UP`. From the BSP — `cc1101_bus_init()`, `cc1101_bind()`, `cc1101_probe()`, `cc1101_bringup()`, `cc1101_set_frequency/modulation/length_config/packet_length/crc/white_data/sync_word/power()`, `cc1101_idle()`, `cc1101_rx()`, `cc1101_flush_rx()`, `cc1101_send_packet()`, `cc1101_receive_packet()`, `cc1101_rx_bytes_available()`, `cc1101_rssi_dbm()`, `board_watchdog_kick()`. From core — `beacon_pack()`, `beacon_msg_t`, `BST_IDLE`.
- Produces: file-scope state Task 3 reads — `s_radio[2]`, the index constants `WD_TXR` (0) and `WD_RXR` (1), `s_flags`, `s_selftest`, `s_rssi`, `s_lqi`.

- [ ] **Step 1: Add `src/link/` to both OG executables**

In `cmake/board_og.cmake`, add `src/link/wilidoro_link.c` to the `wilidoro_display` source list (immediately after `src/hal/hal_og.c`) and to the `wilidoro_main` source list (after `src/main_og/main.c`). Add `src/link` to **both** `target_include_directories` lists.

`wilidoro_main`'s block becomes:

```cmake
add_executable(wilidoro_main
    src/main_og/main.c
    src/link/wilidoro_link.c
    src/core/beacon.c
)
target_include_directories(wilidoro_main PRIVATE
    src/core src/hal src/app src/main_og src/link
)
```

`src/core/beacon.c` is added because the self-test transmits a real `beacon_pack()` frame rather than an invented payload.

- [ ] **Step 2: Add the radio bring-up and self-test**

In `src/main_og/main.c`, add these includes below the existing `#include "fwog_main.h"`:

```c
#include "radio/cc1101.h"
#include "wilidoro_link.h"
#include "beacon.h"
#include <string.h>
```

Then, above `main()`:

```c
/* The reference's own SPI rate -- obSpiRadio.init(1'000'000, 8, 0, 0). Well
   inside the datasheet's 10 MHz single-access ceiling; preserved rather than
   raised, since nothing here is throughput-bound. */
#define WD_RADIO_SPI_HZ 1000000u

/* 433.92 MHz: already BEACON_HZ on the FreeWili 2, and the ONE band needing no
   expander traffic -- fwog_ioexp_init() (inside the display CPU's board_init())
   comes up at FWOG_ANT_400MHZ for both radios. Changing this without also
   steering the PCAL6416 keys the radios into the wrong antenna path. */
#define WD_BEACON_HZ 433920000u

/* -30 dBm is the lowest PA-table entry and bench_main's proven setting: ample
   across the few centimetres between the two on-board antennas, minimal
   emission, and it leaves headroom to SEE attenuation rather than swamping it.
   The transmitter is raised to WD_OPERATING_DBM only AFTER the self-test, so
   the self-test's RSSI stays comparable to bench_main's measurements. */
#define WD_SELFTEST_DBM  (-30)
#define WD_OPERATING_DBM 0

#define WD_SYNC_HI 0x57u   /* 'W' */
#define WD_SYNC_LO 0x44u   /* 'D', matching BEACON_MAGIC0/1 */

#define WD_SELFTEST_WAIT_MS 300u

/* CS0 transmits, CS1 listens. Named by CHIP SELECT, never by an ordinal
   "radio 1/2": cc1101.h records that the reference's variable names, the
   schematic silkscreen and the pin suffixes all disagree with each other. */
enum { WD_TXR = 0, WD_RXR = 1 };

static cc1101_t     s_radio[2];
static uint8_t      s_flags;      /* WD_STATUS_CS0_UP | WD_STATUS_CS1_UP */
static wd_selftest_t s_selftest = WD_SELFTEST_NOT_RUN;
static int8_t       s_rssi;
static uint8_t      s_lqi;

/* Everything NOT set here is left at whatever cc1101_bringup()'s register bank
   applies -- data rate, deviation, RX bandwidth, AGC, front end. That bank is
   the configuration bench_main proved on the air on this board; departing from
   it without a measurement trades a known-good state for a guess. */
static bool configure_radio(cc1101_t *r, int dbm) {
    bool ok = cc1101_probe(r);                        /* PARTNUM 0x00, VERSION 0x14 */
    ok = ok && cc1101_bringup(r);
    ok = ok && cc1101_set_frequency(r, WD_BEACON_HZ);
    ok = ok && cc1101_set_modulation(r, CC1101_MOD_2FSK);
    ok = ok && cc1101_set_length_config(r, 0u);       /* 0 = fixed length */
    ok = ok && cc1101_set_packet_length(r, (uint8_t)BEACON_WIRE_LEN);
    ok = ok && cc1101_set_crc(r, true);
    ok = ok && cc1101_set_white_data(r, true);
    ok = ok && cc1101_set_sync_word(r, WD_SYNC_HI, WD_SYNC_LO);
    ok = ok && cc1101_set_power(r, dbm);
    return ok;
}

/* Ported from bench_main's loop_once(). Transmits a real beacon frame on CS0
   and listens for it on CS1 -- over the air, which the FreeWili 2's same-pad
   loopback could never be. */
static wd_selftest_t run_selftest(int8_t *out_rssi, uint8_t *out_lqi) {
    *out_rssi = 0;
    *out_lqi = 0;

    beacon_msg_t m;
    memcpy(m.name, "SELFTEST", BEACON_NAME_LEN);   /* exactly 8, not NUL-terminated */
    m.state = BST_IDLE;
    m.minutes_left = 0;
    m.completed = 0;
    uint8_t wire[BEACON_WIRE_LEN];
    beacon_pack(&m, wire);

    /* Receiver first, and flushed: a stale FIFO would otherwise be read as
       this attempt's reply. */
    cc1101_flush_rx(&s_radio[WD_RXR]);
    if (!cc1101_rx(&s_radio[WD_RXR])) return WD_SELFTEST_NO_RX;

    if (!cc1101_send_packet(&s_radio[WD_TXR], wire, (uint8_t)BEACON_WIRE_LEN))
        return WD_SELFTEST_NO_KEY;

    const absolute_time_t deadline = make_timeout_time_ms(WD_SELFTEST_WAIT_MS);
    int raw = 0;
    while (!time_reached(deadline)) {
        board_watchdog_kick();          /* REQUIRED: 2 s watchdog, no other recovery */
        raw = cc1101_rx_bytes_available(&s_radio[WD_RXR]);
        if (raw > 0 && (raw & 0x7F) > 0) break;
    }
    const unsigned n = (raw > 0) ? (unsigned)(raw & 0x7F) : 0u;
    if (n == 0u) return WD_SELFTEST_NO_RX;

    /* Fixed-length mode puts NO length byte in the FIFO, so the payload starts
       at offset 0 -- unlike bench_main, which runs variable-length and reads
       buf[0] as a count. Two appended status bytes follow (PKTCTRL1
       APPEND_STATUS, left enabled by the bringup bank). */
    uint8_t buf[BEACON_WIRE_LEN + 2];
    const unsigned want = (n > sizeof buf) ? (unsigned)sizeof buf : n;
    if (cc1101_receive_packet(&s_radio[WD_RXR], buf, (uint8_t)want) < 0)
        return WD_SELFTEST_NO_RX;

    /* Per-PACKET RSSI and LQI from the appended status bytes -- not the
       free-running RSSI register, which reads whatever the channel held when
       it was sampled. These are the numbers that answer the RF question. */
    if (want >= BEACON_WIRE_LEN + 2u) {
        *out_rssi = (int8_t)cc1101_rssi_dbm((int8_t)buf[BEACON_WIRE_LEN]);
        *out_lqi = buf[BEACON_WIRE_LEN + 1u];
    }
    if (want < BEACON_WIRE_LEN || memcmp(buf, wire, BEACON_WIRE_LEN) != 0)
        return WD_SELFTEST_CORRUPT;
    return WD_SELFTEST_PASS;
}

static const char *selftest_text(wd_selftest_t s) {
    switch (s) {
    case WD_SELFTEST_PASS:    return "pass";
    case WD_SELFTEST_NO_KEY:  return "FAIL transmitter never keyed";
    case WD_SELFTEST_NO_RX:   return "DEGRADED keyed but heard nothing";
    case WD_SELFTEST_CORRUPT: return "DEGRADED heard but payload wrong";
    default:                  return "not run";
    }
}

/* Runs once, after fwog_display_update_run(). */
static void radio_init(void) {
    cc1101_bus_init(WD_RADIO_SPI_HZ);
    cc1101_bind(&s_radio[WD_TXR], CC1101_RADIO_CS0);
    cc1101_bind(&s_radio[WD_RXR], CC1101_RADIO_CS1);

    board_watchdog_kick();
    const bool cs0 = configure_radio(&s_radio[WD_TXR], WD_SELFTEST_DBM);
    board_watchdog_kick();
    const bool cs1 = configure_radio(&s_radio[WD_RXR], WD_SELFTEST_DBM);
    board_watchdog_kick();

    s_flags = (uint8_t)((cs0 ? WD_STATUS_CS0_UP : 0u) | (cs1 ? WD_STATUS_CS1_UP : 0u));
    cc1101_idle(&s_radio[WD_TXR]);

    if (cs0 && cs1) s_selftest = run_selftest(&s_rssi, &s_lqi);

    /* Operating power only now -- see WD_SELFTEST_DBM. */
    if (cs0) cc1101_set_power(&s_radio[WD_TXR], WD_OPERATING_DBM);
    if (cs1) cc1101_rx(&s_radio[WD_RXR]);   /* park the receiver, permanently */

    DIAG("[wilidoro] radio cs0=%s cs1=%s selftest=%s rssi=%d lqi=%u crc=%d\n",
         cs0 ? "ok" : "FAILED", cs1 ? "ok" : "FAILED",
         selftest_text(s_selftest), (int)s_rssi,
         (unsigned)(s_lqi & 0x7Fu), (s_lqi & 0x80u) ? 1 : 0);
}
```

- [ ] **Step 3: Call it from `main()`**

In `src/main_og/main.c`, immediately after the existing `const fwog_display_result_t disp = fwog_display_update_run();` line, add:

```c
    /* The radios come up regardless of how the display handoff went. If the
       link is down the USB console is the only readout there is, which is
       exactly when the self-test result matters most. */
    radio_init();
```

- [ ] **Step 4: Build both device targets**

Run: `powershell -File tools/build_og.ps1`
Expected: SUCCESS. Both `wilidoro_display` and `wilidoro_main` link.

Then run: `powershell -File tools/build.ps1`
Expected: SUCCESS, and unchanged — the FW2 build must not be affected by any of this.

- [ ] **Step 5: Confirm the host suite is still green**

Run: `powershell -File tools/test.ps1`
Expected: 12/12 PASS.

- [ ] **Step 6: Commit**

```bash
git add src/main_og/main.c cmake/board_og.cmake
git commit -m "feat(og): bring both CC1101s up at 433.92 MHz and self-test over the air"
```

**If the self-test fails on hardware — the one prepared fallback.**

Fixed-length packet mode is the single place this plan knowingly leaves the
configuration `bench_main` measured on this board; `bench_main` proved
*variable*-length, reading `buf[0]` as the count. If the self-test reports
`DEGRADED keyed but heard nothing` or `DEGRADED heard but payload wrong` while
`bench_main`'s own `rf` + `loop` still pass on the same board, suspect this
before suspecting the RF path, and switch to the exact configuration already
known to work:

- `configure_radio()`: `cc1101_set_length_config(r, 1u)` (1 = variable) and drop
  the `cc1101_set_packet_length()` call.
- Transmit a 17-byte buffer whose byte 0 is `0x10` (16) followed by the wire
  frame.
- On receive, expect `1 + BEACON_WIRE_LEN + 2` bytes, check `buf[0] == 16`, and
  read the payload from offset **1**; the status bytes shift to offsets
  `1 + BEACON_WIRE_LEN` and `2 + BEACON_WIRE_LEN`.

Do not make this change speculatively. It costs a byte per frame and is only
worth it if the measurement says so.

---

### Task 3: Main CPU — the relay loop

**Files:**
- Modify: `src/main_og/main.c`

**Interfaces:**
- Consumes: from Task 2 — `s_radio[]`, `WD_TXR`, `WD_RXR`, `s_flags`, `s_selftest`, `s_rssi`, `s_lqi`. From Task 1 — `wd_link_type()`, `wd_link_parse_beacon()`, `wd_link_build_beacon()`, `wd_link_build_status()`, `WD_LINK_MSG_BEACON_TX`, `WD_LINK_MSG_BEACON_RX`. From the BSP — `fwog_link_rx_t`, `fwog_link_rx_init()`, `fwog_link_rx_byte()`, `fwog_link_uart_read()`, `fwog_link_uart_send_frame()`.
- Produces: nothing consumed by later tasks. This closes the main-CPU half.

- [ ] **Step 1: Add the relay state and helpers**

In `src/main_og/main.c`, add this include with the others:

```c
#include "common/link/link_frame.h"
```

Then, below `radio_init()`:

```c
/* Main's own receive state. fwog_display_update_run() already called
   fwog_link_uart_init() (display_update.c:185) and does NOT deinit, so the
   UART is up -- but its rx state is internal to that module, so we need our
   own. */
static fwog_link_rx_t s_link_rx;

#define WD_STATUS_PERIOD_MS 5000u
/* Bytes drained per pass. Bounded so a babbling link cannot stall the loop
   past its 2 s watchdog. */
#define WD_DRAIN_BUDGET 256u

static void send_status(void) {
    uint8_t p[sizeof(wd_link_status_t)];
    const size_t n = wd_link_build_status(p, sizeof p, s_flags,
                                          (uint8_t)s_selftest, s_rssi, s_lqi);
    if (n != 0u) (void)fwog_link_uart_send_frame(p, n);
}

/* Drain the link and transmit any 0x40 the display sent down. */
static void service_link(void) {
    uint8_t b;
    size_t len = 0;
    for (unsigned i = 0; i < WD_DRAIN_BUDGET && fwog_link_uart_read(&b); i++) {
        if (!fwog_link_rx_byte(&s_link_rx, b, &len)) continue;
        if (wd_link_type(s_link_rx.buf, len) != WD_LINK_MSG_BEACON_TX) continue;
        uint8_t wire[BEACON_WIRE_LEN];
        if (!wd_link_parse_beacon(s_link_rx.buf, len, wire)) continue;
        if (!(s_flags & WD_STATUS_CS0_UP)) continue;   /* no transmitter */
        (void)cc1101_send_packet(&s_radio[WD_TXR], wire, (uint8_t)BEACON_WIRE_LEN);
    }
}

/* Forward anything CS1 heard. */
static void poll_radio(void) {
    if (!(s_flags & WD_STATUS_CS1_UP)) return;
    const int raw = cc1101_rx_bytes_available(&s_radio[WD_RXR]);
    if (raw < 0) return;                       /* SPI timeout */
    if (raw & 0x80) {                          /* RXFIFO_OVERFLOW */
        cc1101_flush_rx(&s_radio[WD_RXR]);
        cc1101_rx(&s_radio[WD_RXR]);
        return;
    }
    const unsigned n = (unsigned)(raw & 0x7F);
    if (n < BEACON_WIRE_LEN + 2u) return;      /* not a whole packet yet */

    uint8_t buf[BEACON_WIRE_LEN + 2];          /* payload + RSSI + LQI */
    if (cc1101_receive_packet(&s_radio[WD_RXR], buf, (uint8_t)sizeof buf) < 0) return;

    uint8_t p[sizeof(wd_link_beacon_t)];
    const size_t m = wd_link_build_beacon(p, sizeof p, WD_LINK_MSG_BEACON_RX, buf);
    if (m != 0u) (void)fwog_link_uart_send_frame(p, m);

    /* Re-arm: the bringup bank's MCSM1 returns the radio to IDLE after a
       received packet, so without this CS1 hears exactly one frame per boot. */
    cc1101_rx(&s_radio[WD_RXR]);
}
```

- [ ] **Step 2: Wire it into the main loop**

In `src/main_og/main.c`, initialise the receive state — add immediately after the `radio_init();` call added in Task 2 Step 3:

```c
    fwog_link_rx_init(&s_link_rx);
```

Add a status deadline beside the existing `next_beat` declaration:

```c
    absolute_time_t next_status = make_timeout_time_ms(WD_STATUS_PERIOD_MS);
```

Then inside the existing `while (true)` loop, immediately after the existing `board_watchdog_kick();` call and **before** the `if (time_reached(next_beat))` block, add:

```c
        service_link();
        poll_radio();
        if (time_reached(next_status)) {
            next_status = make_timeout_time_ms(WD_STATUS_PERIOD_MS);
            send_status();
        }
```

The loop's existing trailing `sleep_ms(2)` stays: it caps radio polling at roughly 500 Hz, which is ample against a beacon that repeats every 20 s and a 64-byte RX FIFO.

- [ ] **Step 3: Build both device targets**

Run: `powershell -File tools/build_og.ps1`
Expected: SUCCESS.

Run: `powershell -File tools/build.ps1`
Expected: SUCCESS, FW2 unaffected.

- [ ] **Step 4: Confirm the host suite is still green**

Run: `powershell -File tools/test.ps1`
Expected: 12/12 PASS.

- [ ] **Step 5: Commit**

```bash
git add src/main_og/main.c
git commit -m "feat(og): relay beacons between the link and the air on the main CPU"
```

---

### Task 4: Display CPU — real `hal_beacon_tx/rx` and honest `hal_caps().radio`

**Files:**
- Modify: `src/hal/hal.h`
- Modify: `src/hal/hal_og.c:382-395` (the beacon stubs and `hal_caps`)
- Modify: `src/hal/hal_target.c`
- Modify: `src/hal/hal_sim.c`

**Interfaces:**
- Consumes: from Task 1 — every `wd_link_*` symbol. From the BSP — `fwog_link_uart_init()`, `FWOG_LINK_BAUD`, `fwog_link_rx_init()`, `fwog_link_rx_byte()`, `fwog_link_uart_read()`, `fwog_link_uart_send_frame()`.
- Produces: `void hal_beacon_show_self(bool show)` — the seam Task 5's softkey calls.

- [ ] **Step 1: Add the seam to `hal.h`**

In `src/hal/hal.h`, immediately below the existing `bool hal_beacon_rx(...)` declaration (line 55), add:

```c
/* Show or hide this device's own beacon echoing back. OG only: its receiver
   sits centimetres from its transmitter, so it hears everything it sends, and
   would otherwise list itself as a neighbour. Default is HIDDEN. A no-op on
   boards that cannot hear themselves. */
void hal_beacon_show_self(bool show);
```

- [ ] **Step 2: Implement the display half**

In `src/hal/hal_og.c`, add these includes below the existing `#include "fwog_display.h"`:

```c
#include "wilidoro_link.h"
#include "common/link/link_uart.h"
#include "common/link/link_frame.h"
#include <string.h>
```

Replace the whole `/* ---- Beacon: Plan OG-D, over the inter-CPU link ---- */` block at `src/hal/hal_og.c:382-384` with:

```c
/* ---- Beacon: Plan OG-D, over the inter-CPU link ----
 *
 * This CPU has no radio. hal_beacon_tx/rx are messages to the main CPU, which
 * owns both CC1101s. Nothing above the HAL knows the difference: beacon_rx.c,
 * the neighbour table and the Nearby screen are untouched. */

/* fwog_link_rx_t embeds a 4160-byte payload buffer, so this is 4168 bytes --
   the single largest allocation Plan OG-D adds to this CPU. Against the Plan
   OG-A baseline of 152,188 B of 264 KB that is 1.6 %. */
static fwog_link_rx_t s_link_rx;
static bool           s_link_up;

static uint8_t  s_last_tx[BEACON_WIRE_LEN];
static bool     s_have_tx;
static uint32_t s_last_tx_ms;
static bool     s_show_self;          /* Nearby's Self softkey; default hidden */

static wd_link_status_t s_status;
static bool             s_status_valid;
static uint32_t         s_status_ms;

/* Bytes drained per hal_beacon_rx() call. app_poll() calls it every 100 ms, so
   this is several whole frames' worth against a beacon that repeats every 20 s
   -- and short enough that a saturated or babbling link cannot starve the LVGL
   tick. */
#define WD_DRAIN_BUDGET 256u

/* How long a 0x42 stays believed. Main resends every 5 s, so this tolerates
   two consecutive losses before hal_caps().radio goes false. */
#define WD_STATUS_STALE_MS 15000u

void hal_beacon_tx(const uint8_t wire[BEACON_WIRE_LEN]) {
    if (!s_link_up) return;
    memcpy(s_last_tx, wire, BEACON_WIRE_LEN);
    s_last_tx_ms = hal_now_ms();
    s_have_tx = true;
    uint8_t p[sizeof(wd_link_beacon_t)];
    const size_t n = wd_link_build_beacon(p, sizeof p, WD_LINK_MSG_BEACON_TX, wire);
    if (n != 0u) (void)fwog_link_uart_send_frame(p, n);
}

bool hal_beacon_rx(uint8_t wire[BEACON_WIRE_LEN]) {
    if (!s_link_up) return false;
    uint8_t b;
    size_t len = 0;
    for (unsigned i = 0; i < WD_DRAIN_BUDGET && fwog_link_uart_read(&b); i++) {
        if (!fwog_link_rx_byte(&s_link_rx, b, &len)) continue;
        switch (wd_link_type(s_link_rx.buf, len)) {
        case WD_LINK_MSG_STATUS: {
            wd_link_status_t st;
            if (!wd_link_parse_status(s_link_rx.buf, len, &st)) break;
            /* On CHANGE only -- a healthy board must not emit a line every
               5 s. This is the only consumer of the RSSI and LQI fields, and
               it exists so those numbers are reachable when the display's USB
               console is the one plugged in rather than main's. */
            if (!s_status_valid || memcmp(&st, &s_status, sizeof st) != 0) {
                DIAG("[wilidoro] radio cs0=%d cs1=%d selftest=%u rssi=%d lqi=%u crc=%d\n",
                     (st.flags & WD_STATUS_CS0_UP) ? 1 : 0,
                     (st.flags & WD_STATUS_CS1_UP) ? 1 : 0,
                     (unsigned)st.selftest, (int)st.rssi_dbm,
                     (unsigned)(st.lqi & 0x7Fu), (st.lqi & 0x80u) ? 1 : 0);
            }
            s_status = st;
            s_status_valid = true;
            s_status_ms = hal_now_ms();
            break;
        }
        case WD_LINK_MSG_BEACON_RX: {
            uint8_t frame[BEACON_WIRE_LEN];
            if (!wd_link_parse_beacon(s_link_rx.buf, len, frame)) break;
            if (!s_show_self &&
                wd_link_is_echo(s_have_tx, s_last_tx, s_last_tx_ms,
                                hal_now_ms(), frame)) {
                break;                  /* our own echo: drop, keep draining */
            }
            memcpy(wire, frame, BEACON_WIRE_LEN);
            return true;
        }
        default:
            break;                       /* not ours -- the BSP's, or noise */
        }
    }
    return false;
}

void hal_beacon_show_self(bool show) { s_show_self = show; }
```

- [ ] **Step 3: Bring the link up, and make `hal_caps().radio` honest**

In `src/hal/hal_og.c`, at the end of `hal_init()` (after the `s_imu_ok = lis3dh_configure(...)` line), add:

```c
    /* The display BOOTLOADER deinits the link before jumping here (bl_jump.c),
       so the application must bring it back up itself -- board_init() does not.
       apps/bench_display/main.c:981 is the reference for these two lines. */
    s_link_up = fwog_link_uart_init(FWOG_LINK_BAUD);
    fwog_link_rx_init(&s_link_rx);
    s_show_self = false;
    if (!s_link_up) DIAG("[wilidoro] link uart init FAILED -- no beacon\n");
```

Then replace the `c.radio = false;   /* Plan OG-D */` line in `hal_caps()` with:

```c
    /* True only while main is actually saying so. Requiring BOTH radios is
       deliberate: a dead CS1 means Nearby can never populate, and a dead CS0
       means we are invisible to everyone else -- either way "radio: ok" on the
       Settings screen would be a lie. Goes false on its own if the main CPU
       dies or the link breaks, not only if a radio is absent. */
    c.radio = s_status_valid
           && (uint32_t)(hal_now_ms() - s_status_ms) <= WD_STATUS_STALE_MS
           && (s_status.flags & (WD_STATUS_CS0_UP | WD_STATUS_CS1_UP))
              == (WD_STATUS_CS0_UP | WD_STATUS_CS1_UP);
```

- [ ] **Step 4: Add the no-op stubs to the other two backends**

In `src/hal/hal_target.c`, immediately after the existing `hal_beacon_rx()` definition (around line 441), add:

```c
/* The FreeWili 2 has one radio and cannot hear itself, so there is no echo to
   show or hide. Present only to satisfy the hal.h seam. */
void hal_beacon_show_self(bool show) { (void)show; }
```

In `src/hal/hal_sim.c`, immediately after the last `hal_beacon_rx()` definition, add:

```c
/* The simulator does not model the echo, so this records nothing. The OG sim
   still renders Nearby's Self softkey and still toggles it -- it simply has no
   observable effect there. Teaching hal_sim.c to model the beacon is a
   non-goal of Plan OG-D. */
void hal_beacon_show_self(bool show) { (void)show; }
```

- [ ] **Step 5: Build all three configurations**

Run: `powershell -File tools/build_og.ps1`
Expected: SUCCESS. Check the `--print-memory-usage` RAM line against the Plan OG-A baseline of 152,188 B — the increase should be roughly 4.2 KB (the `fwog_link_rx_t`) plus a few hundred bytes. If it is dramatically more, stop and find out why before continuing.

Run: `powershell -File tools/build.ps1`
Expected: SUCCESS, FW2 unaffected.

Run: `powershell -File tools/sim.ps1 -Board og`
Expected: the simulator builds and runs. Close the window.

- [ ] **Step 6: Confirm the host suite is still green**

Run: `powershell -File tools/test.ps1`
Expected: 12/12 PASS.

- [ ] **Step 7: Commit**

```bash
git add src/hal/hal.h src/hal/hal_og.c src/hal/hal_target.c src/hal/hal_sim.c
git commit -m "feat(og): real hal_beacon_tx/rx over the link, and an honest caps.radio"
```

---

### Task 5: The `Self` softkey on Nearby

**Files:**
- Modify: `src/ui/screen_nearby.c`

**Interfaces:**
- Consumes: `hal_beacon_show_self(bool)` from Task 4; `app()->settings.name` and `APP_NAME_LEN` from `src/app/app_model.h`; `ui_softkey_set_labels()` and `theme_get()`, already used in this file.
- Produces: nothing. This is the last code task.

- [ ] **Step 1: Add the include and the toggle state**

In `src/ui/screen_nearby.c`, add to the include block:

```c
#include "hal.h"
```

Below the existing `static lv_obj_t *s_scr, *s_list, *s_bar, *s_empty, *s_title;` line, add:

```c
#if defined(WILIDORO_BOARD_OG)
/* The OG's receiver sits centimetres from its transmitter, so it hears every
   beacon it sends. Hidden by default -- a device that lists itself confuses
   everyone who is not currently testing it -- but one button press away,
   because showing it makes ONE board prove the whole chain: beacon_pack, the
   link, CS0 keying, the air, CS1, and a row on this screen.

   Deliberately NOT persisted in app_settings_t: this is a diagnostic, and the
   settings save path only just had a bug fixed (858454a). */
static bool s_show_self;
#endif
```

- [ ] **Step 2: Label the softkey**

In `screen_nearby_create()`, replace these two lines:

```c
    const char *lbl[5] = {"Back",0,0,0,0};
    ui_softkey_set_labels(s_bar, lbl);
```

with:

```c
#if defined(WILIDORO_BOARD_OG)
    s_show_self = false;
    hal_beacon_show_self(false);
    /* Column 4 is free -- this screen has only ever used column 0. The label
       does not encode on/off because the LIST is the state readout, and
       because 60 px buttons at a 16 px font are why "Dismiss" already clips
       (docs/hardware-notes.md, "softkey label clipping"). */
    const char *lbl[5] = {"Back",0,0,0,"Self"};
#else
    const char *lbl[5] = {"Back",0,0,0,0};
#endif
    ui_softkey_set_labels(s_bar, lbl);
```

- [ ] **Step 3: Handle the press**

Replace `screen_nearby_softkey()` entirely:

```c
void screen_nearby_softkey(int col) {
    if (col == 0) { app_goto(SCREEN_TIMER); return; }
#if defined(WILIDORO_BOARD_OG)
    if (col == 4) {
        s_show_self = !s_show_self;
        hal_beacon_show_self(s_show_self);
    }
#endif
}
```

- [ ] **Step 4: Tint our own row**

In `screen_nearby_update()`, replace this line:

```c
        lv_obj_set_style_text_color(name, lv_color_hex(UI_TEXT), 0);
```

with:

```c
        /* Our own echo, when shown, is drawn in the accent colour so there is
           no ambiguity about which row is us. A genuine neighbour that has
           taken our name gets tinted too -- which is honest, not a bug: the
           two are indistinguishable on the air. */
        const bool is_self = (memcmp(n->name, app()->settings.name, APP_NAME_LEN) == 0);
        lv_obj_set_style_text_color(name,
            lv_color_hex(is_self ? theme_get(app()->settings.theme)->accent : UI_TEXT), 0);
```

- [ ] **Step 5: Build all three configurations**

Run: `powershell -File tools/build_og.ps1`
Expected: SUCCESS.

Run: `powershell -File tools/build.ps1`
Expected: SUCCESS. The FW2 build must still show only `Back` on Nearby.

Run: `powershell -File tools/sim.ps1 -Board og`
Expected: builds and runs. Navigate to Nearby and confirm **`Self` renders on column 4 without clipping** — it is four characters against `Dismiss`'s seven, so it should be comfortable, but this is the cheapest place to find out otherwise. Close the window.

- [ ] **Step 6: Confirm the host suite is still green**

Run: `powershell -File tools/test.ps1`
Expected: 12/12 PASS.

- [ ] **Step 7: Commit**

```bash
git add src/ui/screen_nearby.c
git commit -m "feat(og): a Self softkey on Nearby to reveal this device's own beacon"
```

---

### Task 6: The on-device checklist, and what still needs a second board

**Files:**
- Modify: `docs/hardware-notes.md`

**Interfaces:**
- Consumes: nothing. Documentation of what Tasks 1–5 built and what remains unproven.
- Produces: nothing.

- [ ] **Step 1: Append the section**

Add to the end of `docs/hardware-notes.md`, matching the format of the existing "### On-device beacon checklist" section written for the FreeWili 2:

```markdown
## FreeWili OG — the sub-GHz beacon (Plan OG-D)

Built 2026-07-29. **Status: not yet run on a board.** Every claim below is a
thing to check, not a thing checked.

Design: `docs/superpowers/specs/2026-07-29-wilidoro-OG-D-beacon-design.md`.

The display CPU has no radio, so `hal_beacon_tx/rx` are messages over the
inter-CPU UART link (`0x40` transmit, `0x41` received, `0x42` radio status).
The main CPU owns both CC1101s at 433.92 MHz: `CS0` transmits, `CS1` sits
permanently in RX.

**433.92 MHz needs no expander traffic.** `fwog_ioexp_init()`, inside the
display CPU's `board_init()`, comes up at `FWOG_ANT_400MHZ` for both radios.
Anything that retunes to another band must also steer the PCAL6416
(`fwog_ioexp_link_set_antennas()`) or it keys the radios into the wrong antenna
path.

### On-device beacon checklist

Flash with `powershell -File tools/flash_og.ps1`. Never `fw flash
wilidoro_display`.

1. **The main console reports the self-test.** One line at boot:
   `[wilidoro] radio cs0=ok cs1=ok selftest=pass rssi=<n> lqi=<n> crc=1`.
   Record the RSSI and LQI here — those numbers are the measured answer to
   whether two antennas centimetres apart saturate rather than demodulate.
   `selftest=FAIL transmitter never keyed` and `selftest=DEGRADED keyed but
   heard nothing` are different faults: the first is a synthesiser that will
   not lock, the second is an RF path that carried nothing.
2. **The display console echoes it**, on change only, so a healthy board prints
   it once rather than every 5 s.
3. **Settings shows the radio.** With both CC1101s up it must not say "no
   radio". Pull the main CPU into BOOTSEL (`fw bootsel --cpu main`) and within
   15 s it must start saying so — that is the `0x42` liveness window working.
4. **Nearby is empty by default**, with a single board. The device's own echo
   is dropped.
5. **Press `Self` on Nearby (column 4).** Within one beacon period (~20 s) our
   own name must appear, drawn in the theme accent colour. This is the whole
   chain proven end to end on one board: `beacon_pack` → `0x40` → CS0 keys →
   over the air → CS1 → `0x41` → `beacon_unpack` → a row on screen.
6. **Press `Self` again.** The row must disappear within `neighbor_expire()`'s
   timeout.
7. **`Self` must not clip.** Four characters against `Dismiss`'s seven on a
   60 px button — see "softkey label clipping" above.
8. **Turn the beacon off in Settings.** Nearby must stop gaining rows.

### Two things still need a second board

Neither is done, and neither can be faked by one device:

1. **Two distinct names coexisting** in the neighbour table.
2. **`neighbor_expire()` aging a real entry out** when another device leaves.

### Known limitation

In the **OG simulator**, Nearby's `Self` softkey renders and toggles but has no
observable effect: `hal_sim.c` does not model the echo. Teaching the simulator
to model the beacon was explicitly out of scope for Plan OG-D.
```

- [ ] **Step 2: Commit**

```bash
git add docs/hardware-notes.md
git commit -m "docs(og): the OG beacon checklist, and what still needs a second board"
```

---

## After the plan

Do **not** claim the beacon works. Every task above is verified by builds and
host tests; the radio itself has been verified by nobody. The honest statement
at the end of Task 6 is: *built, compiles, host-tested, never seen on hardware* —
exactly the distinction `docs/hardware-notes.md` already draws for Plan OG-C.

Then: flash, walk the Task 6 checklist, record the self-test's RSSI and LQI in
`docs/hardware-notes.md`, and run the scoped review before merging `og-d-beacon`.
