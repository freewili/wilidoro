/* FreeWili OG main CPU: wilidoro's radio half.
 *
 * In Plan A this exists for one reason -- fwog_display_update_run() is how the
 * display application reaches the display CPU, which has no BOOTSEL button and
 * must never be UF2-flashed directly. Plan OG-D adds the CC1101 beacon. */
#include "fwog_main.h"
#include "radio/cc1101.h"
#include "wilidoro_link.h"
#include "beacon.h"
#include "common/link/link_frame.h"
#include "pico/stdlib.h"
#include <string.h>

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

static cc1101_t      s_radio[2];
static uint8_t       s_flags;      /* WD_STATUS_CS0_UP | WD_STATUS_CS1_UP */
static wd_selftest_t s_selftest = WD_SELFTEST_NOT_RUN;
static int8_t        s_rssi;
static uint8_t       s_lqi;

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

    /* Operating power only now -- see WD_SELFTEST_DBM. Return value
       deliberately unchecked: a failure here leaves the transmitter at
       -30 dBm, which is degraded but working, and 0x42 still reports the
       radio up because it is. */
    if (cs0) (void)cc1101_set_power(&s_radio[WD_TXR], WD_OPERATING_DBM);
    if (cs1) (void)cc1101_rx(&s_radio[WD_RXR]);   /* park the receiver, permanently */

    DIAG("[wilidoro] radio cs0=%s cs1=%s selftest=%s rssi=%d lqi=%u crc=%d\n",
         cs0 ? "ok" : "FAILED", cs1 ? "ok" : "FAILED",
         selftest_text(s_selftest), (int)s_rssi,
         (unsigned)(s_lqi & 0x7Fu), (s_lqi & 0x80u) ? 1 : 0);
}

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
        (void)cc1101_rx(&s_radio[WD_RXR]);
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
    (void)cc1101_rx(&s_radio[WD_RXR]);
}

int main(void) {
    board_init();

    /* Push the embedded display image if the display CPU's copy differs
       (compared by image CRC32, so an unchanged image is skipped). This also
       performs board_release_display() itself, so we must not call it. */
    const fwog_display_result_t disp = fwog_display_update_run();

    /* The radios come up regardless of how the display handoff went. If the
       link is down the USB console is the only readout there is, which is
       exactly when the self-test result matters most. */
    radio_init();
    fwog_link_rx_init(&s_link_rx);

    /* Announce the transfer result for the first 10 s rather than once. The
       handshake finishes before USB CDC has enumerated and the host has
       asserted DTR, and pico_stdio_usb DROPS anything written before then --
       a single DIAG here is written into the void every time. */
    const absolute_time_t announce_until = make_timeout_time_ms(10000);

    absolute_time_t next_beat = make_timeout_time_ms(1000);
    absolute_time_t next_status = make_timeout_time_ms(WD_STATUS_PERIOD_MS);
    while (true) {
        /* REQUIRED. board_init() arms a 2 s watchdog and it is the only way
           to recover a hung main CPU on this board, so the BSP makes kicking
           it the app's job (bsp/main_cpu/watchdog/watchdog.h). Omitting this
           does not fail to build or fail to run -- main simply resets every
           2 s forever, taking the display with it through board_init()'s
           GUI_NRESET. That is measured, not feared: it is what this file did
           on the first hardware bring-up. */
        board_watchdog_kick();

        service_link();
        poll_radio();
        if (time_reached(next_status)) {
            next_status = make_timeout_time_ms(WD_STATUS_PERIOD_MS);
            send_status();
        }

        if (time_reached(next_beat)) {
            next_beat = make_timeout_time_ms(1000);
            if (!time_reached(announce_until)) {
                DIAG("[wilidoro] main alive, display: %s\n",
                     fwog_display_result_text(disp));
            } else {
                DIAG("[wilidoro] main alive\n");
            }
        }
        sleep_ms(2);
    }
}
