# Wilidoro OG-D — the sub-GHz beacon

**Date:** 2026-07-29
**Status:** Designed, not built. This is the last planned piece of the OG port;
Plans OG-A, OG-B and OG-C are merged on `main`.

Give the FreeWili OG a working focus beacon: transmit our pomodoro state over
the CC1101, hear other devices doing the same, and populate the Nearby screen
that OG-C already built but which has shown "no devices heard" on every board
it has ever run on, because `hal_beacon_rx()` hard-returns `false`
(`src/hal/hal_og.c:384`).

This spec supersedes the "Beacon across two CPUs" section of
`2026-07-28-wilidoro-og-port-design.md` where the two disagree. That section
fixed the architecture and remains correct; what it did not have was the
evidence below.

## What changed since the port design was written

Three facts, none of which were available when the og-port design fixed this
architecture. Each removes work or removes risk.

**1. The two-radio over-the-air path already works on this board.**
`wiliOGbsp/apps/bench_main/main.c` has `rf` (bring both CC1101s up at
433.92 MHz, −30 dBm) and `loop` (transmit on one chip select, receive on the
other, reporting per-packet RSSI, LQI and CRC-OK from the appended status
bytes). It has been run and packets looped. The og-port design's ranked risk 5
— "near-field self-test saturation", two antennas inches apart swamping the
receiver rather than demodulating — is **retired by measurement**, not by
argument. `loop_once()` is the template the self-test here is ported from.

**2. The antenna path needs no work at all at 433.92 MHz.**
`fwog_ioexp_init()` runs inside the display CPU's `board_init()` and brings the
PCAL6416 up at `fwog_ioexp_default()`, which is `FWOG_ANT_400MHZ` for **both**
radios. 433.92 MHz is inside that path, and it is already `BEACON_HZ` on FW2
(`src/hal/hal_target.c:142`). So this plan sends no expander traffic, adds no
opcode for antenna steering, and never touches `fwog_io_dir_apply()` — whose
`FWOG_IO_ERR_FPGA_VERIFY` investigation is still open and is not this plan's
problem. `cc1101.h`'s warning that "bring-up of an actual RF link on this board
needs a second piece this driver does not provide" is satisfied by the default,
not by new code.

If a future plan wants another band, `fwog_ioexp_link_set_antennas()`
(`display_cpu/io_expander/ioexp_link.h`) changes only the two antenna fields
and leaves the nine shifter directions alone. That is a one-call change, and
deliberately not made here.

**3. `hal_caps()` has a real consumer on the OG now.**
The og-port design says `screen_settings.c` "is compiled out entirely on the
OG". That was true when it was written and is false today — OG-C ported
Settings, and the file carries per-board `#if defined(WILIDORO_BOARD_OG)`
blocks. So a hardcoded `caps.radio = true` would put a visible lie on the
device screen whenever a CC1101 fails. That is what motivates opcode `0x42`
below, which the og-port design does not have.

## Decisions

1. **Static radio split.** `CC1101_RADIO_CS0` transmits; `CC1101_RADIO_CS1`
   sits permanently in RX. This is exactly what `bench_main`'s `loop_once()`
   does, i.e. the arrangement already proven on this board. The main loop stays
   a flat poll with no radio state machine, which matters because that loop
   also kicks a 2 s watchdog. Rejected: both radios listening with transmit on
   either (buys receive diversity a 20 s beacon does not need, at the cost of
   real state to get wrong), and single-radio CS0-only (throws away the
   over-the-air self-test, which is the one thing making this better than
   FW2's same-pad loopback).
2. **Three opcodes, not two.** `0x40`/`0x41` as the og-port design specifies,
   plus `0x42 RADIO_STATUS` so `hal_caps().radio` can tell the truth. See
   fact 3 above.
3. **`0x42` is periodic, not one-shot.** Every 5 s, forever. Main brings its
   radios up before the display CPU has finished booting the image
   `fwog_display_update_run()` just handed it, so a single announcement would
   be shouted at a display that is not listening yet. `src/main_og/main.c`
   already carries this exact pattern, for the same class of reason (USB CDC
   enumeration finishing after the first `DIAG`). Periodicity also buys
   liveness: `caps.radio` becomes "a good `0x42` arrived within 15 s", which
   correctly goes false if the main CPU dies or the link breaks, not only if a
   radio is absent.
4. **The device drops its own echo by default.** CS1 is centimetres from CS0's
   antenna, so every beacon this device sends comes straight back. Unsuppressed,
   the OG would list itself in Nearby, indistinguishable from a real neighbour.
   FW2 has the same hazard and solves it by draining its capture ring after
   transmitting (`hal_target.c:347`); this is the OG's equivalent.
5. **A `Self` softkey on Nearby reveals the echo.** Column 4, which is free
   (Nearby uses only column 0, `Back`). Showing the echo makes one board prove
   the entire chain end to end — see *Testing*. It is a diagnostic, so it lives
   in RAM and is not persisted: the settings save path only just had a bug
   fixed (`858454a`, "settings save stops wiping stats") and a testing aid is
   not a reason to reopen it.
6. **The echo drop lives on the display CPU**, in `hal_og.c`, not on main. The
   display already knows the sixteen bytes it just sent, so the behaviour is
   identical to dropping on main while needing no protocol field and no state
   kept in sync across the link.
7. **Nothing above the HAL changes**, except the one new softkey.
   `src/core/beacon.c`, `beacon_rx.c`, `src/app/app.c` and the neighbour table
   are untouched. `beacon_ook_encode`/`decode` remain FW2-only; what is shared
   is `beacon_pack`/`beacon_unpack`, whose CRC16 becomes an end-to-end check
   layered over the CC1101's hardware CRC.

## Repository shape

```
src/link/wilidoro_link.h    opcodes, packed structs, pure helpers   (new)
src/link/wilidoro_link.c    build/parse + the echo decision         (new)
src/hal/hal_og.c            display half: real hal_beacon_tx/rx    (edit)
src/main_og/main.c          main half: both CC1101s, self-test     (edit)
src/ui/screen_nearby.c      the Self softkey, OG-only              (edit)
tests/test_wilidoro_link.c  host tests                              (new)
```

`src/link/` is new and is compiled into **all three** binaries —
`wilidoro_display`, `wilidoro_main`, and the host test — which is the point:
an encoding that crosses a link between two separately-flashed CPUs must have
exactly one definition. It has no SDK dependency, so the host test links it
directly.

## The wire protocol

Riding the BSP's existing framing, unchanged: `[SOF 0x7E][len16][payload][crc16]`,
CRC-16/XMODEM over the length and payload. That codec is already host-tested in
the BSP's own suite and measured at zero corrupt bytes across 58 consecutive
256-value sweeps.

Byte 0 of every payload is the message type. `0x00` is invalid, `0x01`–`0x1F`
are frozen for the display bootloader, and `0x20`–`0x22` are the BSP's own
breakout-I/O messages. Ours start at `0x40`, leaving `0x23`–`0x3F` free for the
BSP.

| Opcode | Direction | Payload | Bytes |
|---|---|---|---|
| `0x40` BEACON_TX | display → main | type + `beacon_pack()` frame | 17 |
| `0x41` BEACON_RX | main → display | type + frame heard off the air | 17 |
| `0x42` RADIO_STATUS | main → display | type + flags + self-test result | 5 |

```c
typedef struct __attribute__((packed)) {
    uint8_t type;                      /* 0x40 or 0x41 */
    uint8_t wire[BEACON_WIRE_LEN];     /* 16 */
} wilidoro_link_beacon_t;
_Static_assert(sizeof(wilidoro_link_beacon_t) == 17, "goes on the wire unserialized");

typedef struct __attribute__((packed)) {
    uint8_t type;        /* 0x42 */
    uint8_t flags;       /* bit0 CS0 up, bit1 CS1 up */
    uint8_t selftest;    /* wilidoro_selftest_t */
    int8_t  rssi_dbm;    /* self-test packet RSSI; 0 if not run */
    uint8_t lqi;         /* self-test packet LQI, bit7 = CRC-OK */
} wilidoro_link_status_t;
_Static_assert(sizeof(wilidoro_link_status_t) == 5, "goes on the wire unserialized");
```

Structs go on the wire packed and unserialized, with a `_Static_assert` on each
size, exactly as `io_proto.h`'s do and for the same reason: both ends are
little-endian Cortex-M0+, and `packed` is what makes casting directly into the
receive buffer legal rather than merely lucky.

```c
typedef enum {
    WD_SELFTEST_NOT_RUN = 0,
    WD_SELFTEST_PASS,          /* keyed, heard, payload matched */
    WD_SELFTEST_NO_KEY,        /* cc1101_send_packet() false: GDO0 never
                                  showed sync/end — the TRANSMITTER did not
                                  key. A synthesiser that will not lock. */
    WD_SELFTEST_NO_RX,         /* keyed, but nothing arrived — the RF path
                                  did not carry it. */
    WD_SELFTEST_CORRUPT,       /* arrived, payload did not match */
} wilidoro_selftest_t;
```

`NO_KEY` and `NO_RX` are separate states on `bench_main`'s own authority: it
records that collapsing the two into "received=0" is what made its 315 MHz
result undiagnosable on first measurement.

## The display half — `hal_og.c`

`hal_init()` brings up the link: `fwog_link_uart_init(FWOG_LINK_BAUD)` then
`fwog_link_rx_init()`. **The display app must do this itself** — the display
bootloader deinits the link before jumping to the application (`bl_jump.c`), so
the UART is down on entry. `apps/bench_display/main.c:981` is the reference for
these two lines. A false return is recorded, not ignored: the link is down,
`caps.radio` stays false forever, and it is `DIAG`d.

`hal_beacon_tx(wire)` copies the sixteen bytes into `s_last_tx` with
`hal_now_ms()`, then sends one `0x40` frame. Twenty-two bytes at 12.5 Mbaud is
about 18 µs — worth noting because the same call on FW2 costs roughly 136 ms of
blocking GPIO toggling, which is why `app.c` refuses to transmit while a chime
is sounding. That guard is harmless here and stays; it is shared code and this
plan does not touch it.

`hal_beacon_rx(out)` drains the UART with `fwog_link_uart_read()` into
`fwog_link_rx_byte()`, and for each completed frame:

- `0x42` → latch flags, self-test result and timestamp. When any field differs
  from the previous status, `DIAG` the whole thing — flags, self-test verdict,
  RSSI and LQI — on the *display's* console. That is the only consumer of the
  RSSI and LQI fields, and it exists so the numbers are reachable when the main
  CPU's USB console is not the one plugged in. On change only, so a healthy
  board does not emit a line every 5 s.
- `0x41` → if it is our own echo (see below) and the toggle is off, discard and
  **keep draining**; otherwise copy to `out` and return true.
- anything else → ignore.

The drain is bounded at **256 bytes per call**, or until one `0x41` is ready to
return, whichever comes first — enough for several whole frames at a beacon rate
measured in tens of seconds, and short enough that a saturated or babbling link
cannot starve the LVGL tick. `hal_beacon_rx()` is called from `app_poll()` at
100 ms, matching where FW2 calls it and for the same reason.

`hal_caps().radio` becomes dynamic: true iff a `0x42` arrived within 15 s **and**
both CS0 and CS1 are up. Requiring both is deliberate — a dead CS1 means Nearby
can never populate, and a dead CS0 means we are invisible to everyone else.
Either way "radio: ok" on the Settings screen would be the lie this opcode
exists to prevent.

### RAM

`fwog_link_rx_t` embeds a 4160-byte payload buffer, so one costs 4168 bytes on
the display CPU. The og-port design budgeted 4 KB for it.

*Corrected by measurement during implementation:* **the real cost is 8,376 B,
double that.** `wilidoro_display` went from 162,940 B to 171,316 B (62.16 % →
65.35 % of 256 KB). The missing half is not ours and is not visible at the call
site: `fwog_link_uart_send_frame()` (`link_uart.c:85`) keeps its **own** static
4,165-byte framing buffer, sized for the maximum payload, and it is linked in
the moment that function is called once.

Kept as-is — about 89 KB stays free, and using the BSP's documented helper is
worth more than 4 KB nothing needs. The escape hatch, should RAM ever get
tight, is `fwog_link_encode()` into a small local followed by
`fwog_link_uart_write()`: the largest payload here is 17 bytes, so 22 bytes of
stack replaces the BSP's 4 KB static. Recorded in `docs/hardware-notes.md`.

Note the Plan OG-A baseline of 152,188 B is **not** the right comparison for
this or any later plan — it predates OG-B and OG-C.

## The main half — `src/main_og/main.c`

Order matters. `fwog_display_update_run()` runs first and unchanged: it is how
the display CPU — the one CPU with no BOOTSEL button — receives its image, and
it performs `board_release_display()` itself.

**It also brings the link UART up** (`display_update.c:185`) and does not deinit
it, so main needs only its own `fwog_link_rx_t` and `fwog_link_rx_init()`, not a
second `fwog_link_uart_init()`. If it returned `FWOG_DISP_LINK_DOWN` the UART
was never initialised at all; main still brings the radios up and still runs the
self-test, because with the link down the USB console is the only readout there
is.

Then, once:

```
cc1101_bus_init(1000000)                  /* the reference's own 1 MHz */
cc1101_bind(&r[0], CC1101_RADIO_CS0)
cc1101_bind(&r[1], CC1101_RADIO_CS1)
for each radio:
    cc1101_probe()                        /* PARTNUM 0x00, VERSION 0x14 */
    cc1101_bringup()                      /* reset + register bank + packet mode */
    cc1101_set_frequency(433920000)
    cc1101_set_modulation(CC1101_MOD_2FSK)
    cc1101_set_length_config(0)           /* fixed length */
    cc1101_set_packet_length(16)
    cc1101_set_crc(true)
    cc1101_set_white_data(true)
    cc1101_set_sync_word(0x57, 0x44)      /* 'W','D', matching BEACON_MAGIC0/1 */
    cc1101_set_power(-30)                 /* self-test level; raised below */
cc1101_idle(&r[0])                        /* transmitter parks idle */
cc1101_rx(&r[1])                          /* receiver parks in RX, permanently */

    ... run the self-test at this power ...

cc1101_set_power(&r[0], 0)                /* operating level, after the self-test */
```

Everything not listed is left at whatever `cc1101_bringup()`'s register bank
sets — data rate, deviation, RX bandwidth, AGC, front end. That bank is the
configuration `bench_main` proved on the air on this board, and departing from
it without a measurement would be trading a known-good state for a guess.

**Power, and the order it is set in.** Both radios come up at **−30 dBm**, the
lowest PA-table entry and `bench_main`'s proven setting: ample across a few
centimetres, minimal emission, and it leaves headroom to *see* attenuation
rather than swamping it. The self-test runs at that level. Only afterwards is
the transmitter raised to its operating level of **0 dBm** — far more than a
room needs at 433 MHz, and two orders of magnitude below the driver's +10 dBm
ceiling. Raising CS0 after the self-test rather than before is what keeps the
self-test's RSSI comparable to `bench_main`'s measurements. Both levels are
single constants in `main_og`.

Then loop, kicking `board_watchdog_kick()` every pass:

1. Drain the link. A `0x40` → `cc1101_send_packet(&r[0], wire, 16)`, bounded by
   the driver's own 100 ms GDO0 window. Return value recorded, never assumed.
2. Poll `cc1101_rx_bytes_available(&r[1])`. Overflow bit (bit 7) set → flush and
   re-arm RX. A non-zero count → `cc1101_receive_packet()` for 16 payload bytes
   plus the two appended status bytes, then send `0x41` upward.
3. Every 5 s, send `0x42`.

Note the read length differs from `bench_main`: fixed-length mode puts no length
byte in the FIFO, so the sixteen payload bytes start at offset 0, where
`bench_main` reads `buf[0]` as a count. See risk 2.

## The self-test

Ported from `bench_main`'s `loop_once()`, run once at boot after bring-up:
flush CS1, park it in RX, transmit a real `beacon_pack()` frame on CS0 at
−30 dBm, wait a bounded 300 ms (kicking the watchdog) for `RXBYTES` to go
non-zero, read the packet, compare payload, and pull per-packet RSSI, LQI and
CRC-OK from the appended status bytes rather than the free-running RSSI
register — which reads whatever the channel held when it was sampled, not what
the packet arrived at.

The result goes into every `0x42` and to `DIAG`. A degraded result is reported
as degraded, with its numbers. **It never reports a pass it did not observe** —
the og-port design's rule, and the reason the answer to "does near-field
saturate the receiver" is a measurement on the first run instead of an argument.

## Self-echo and the `Self` toggle

The decision is a pure function, so it can be host-tested:

```c
bool wilidoro_link_is_echo(const uint8_t last_tx[BEACON_WIRE_LEN],
                           uint32_t last_tx_ms, uint32_t now_ms,
                           const uint8_t frame[BEACON_WIRE_LEN]);
```

True when a frame was ever sent, the sixteen bytes match exactly, and it arrived
within **1000 ms** of that transmission. The window is generous by three orders
of magnitude — the round trip is sub-millisecond — and it bounds the damage if
a genuine neighbour ever happens to be byte-identical to us, which requires it
to share our name, state, minutes remaining and completed count.

Splitting this out is the same `#ifndef HOST_TEST` shape `cc1101.c` and
`lis3dh.c` already use, and it keeps `hal_og.c` — which cannot be host-tested —
free of the logic worth testing.

On Nearby, `Self` occupies softkey column 4. Default is **drop**: a device that
lists itself is confusing to everyone not currently testing it. Pressing it
shows the echo, and while shown our own row is drawn in the theme accent colour
so there is no ambiguity about which entry is us. The list is the state readout,
which is why the label does not encode on/off — `Dismiss` already clips on a
60 px button at 16 px (`docs/hardware-notes.md`, "softkey label clipping"), so
`Self:Off` was never available.

Compiled under `#if defined(WILIDORO_BOARD_OG)`, the pattern `app.c` already
uses for DVI. FW2 has one radio and cannot hear itself, so the button would be
dead there.

**Known limitation, stated rather than hidden:** in the *OG simulator* the
button renders and toggles but has no observable effect, because `hal_sim.c`
does not model the echo. Teaching the simulator to model the beacon is a
non-goal here.

## Failure modes

Every one is reported. None is swallowed.

| Failure | Behaviour |
|---|---|
| `fwog_link_uart_init()` fails on display | `caps.radio` false, `hal_beacon_tx` no-ops, Nearby shows "no devices heard", `DIAG` |
| Main CPU dead, or not running our app | No `0x42` → `caps.radio` false within 15 s |
| `fwog_display_update_run()` returned `LINK_DOWN` | Main still brings radios up and self-tests; results to `DIAG` only |
| A CC1101 fails `probe` or `bringup` | `0x42` flags it down; Settings shows no radio |
| Self-test: transmitter never keyed | `WD_SELFTEST_NO_KEY` — distinct from hearing nothing |
| Self-test: keyed, heard nothing | `WD_SELFTEST_NO_RX`, with RSSI. Degraded, not a pass |
| Self-test: heard, payload wrong | `WD_SELFTEST_CORRUPT`, with RSSI and LQI |
| RX FIFO overflow | Flush, re-arm RX, keep counting |
| Corrupt or truncated link frame | Rejected by the BSP's CRC; the decoder resynchronises on the next SOF |
| Link saturated | Per-call drain bound; the LVGL tick is never starved |

Every radio wait is already bounded by the driver
(`CC1101_READY_TIMEOUT_US`, `CC1101_STATE_CHANGE_TIMEOUT_MS`, the 100 ms
packet-TX window). The main loop kicks its 2 s watchdog on every pass,
including inside the self-test's 300 ms wait.

## Testing

**Host tests — one new binary, `tests/test_wilidoro_link.c`.** `link_frame.c` is
pure C with no SDK dependency, so it links into the host test and the round trip
is genuine rather than mocked: build a message, `fwog_link_encode()` it, feed
the bytes one at a time through `fwog_link_rx_byte()`, decode, parse.

- all three opcodes round-tripping intact
- a truncated frame, and a frame with a corrupted CRC
- a payload whose type is not ours (must be ignored, not misparsed)
- a `0x41` whose payload is shorter than the struct
- `wilidoro_link_is_echo()`: exact match inside the window; exact match after it
  expired; a frame differing in exactly one byte; the nothing-sent-yet state

The existing host test binaries keep passing unchanged.

**One board proves nearly everything, and that is a consequence of decision 5.**
With `Self` on, a single device exercises the whole chain: `beacon_pack` → `0x40`
→ CS0 keys → over the air → CS1 → `0x41` → `beacon_unpack` → a row on Nearby.
Every link except "the frame originated on a different device". That is a
materially stronger single-board test than FW2 ever had, where the same-pad
loopback bypassed the demodulator entirely — the one thing FW2's README admits
was never confirmed over the air.

**Two things genuinely need a second board**, and are to be recorded in
`docs/hardware-notes.md` as *not yet done* rather than quietly skipped:

1. Two distinct names coexisting in the neighbour table.
2. `neighbor_expire()` aging a real entry out when a device leaves.

An OG on-device beacon checklist goes in `docs/hardware-notes.md` alongside the
existing FW2 one, in the same format.

## Risks, ranked

1. **Fixed-length packet mode is a departure from the proven configuration.**
   `bench_main` proved variable-length (it reads `buf[0]` as the count) on this
   board; this plan uses fixed 16-byte length, per the og-port design. The
   registers involved (`PKTCTRL0.LENGTH_CONFIG`, `PKTLEN`) are well-trodden and
   the change is small, but it is the one place this plan knowingly leaves the
   measured path. **Fallback:** if fixed length misbehaves, switch to
   variable-length with a leading `0x10` byte, which is byte-for-byte the
   configuration already known to work.
2. **`0x42` liveness could flap.** A 15 s window against a 5 s cadence tolerates
   two consecutive losses. If the link proves lossier than the BSP's measurement
   suggests, `caps.radio` would oscillate and Settings would flicker. Cheap to
   widen; measure before tuning.
3. **Link drain versus LVGL tick.** `hal_beacon_rx()` now does real UART work on
   the 100 ms poll. The bound makes this safe by construction, but the bound
   itself is a number to check against a real frame rate, not to assume.
4. **Self-test at −30 dBm may be too quiet** in some board orientations even
   though it has looped. If so, raise it stepwise and record the level that
   worked — never silently, and never by declaring a pass.

Risks the og-port design listed that this plan **does not** inherit: near-field
saturation (retired by measurement, fact 1), and antenna-path steering (not
needed at 433.92 MHz, fact 2).

## Non-goals

Deliberately excluded, so nothing here is mistaken for missing:

- **Making the simulator model the beacon.** `hal_sim.c`'s OG branch returns
  `false` and continues to; the simulator models neither the link nor the
  radios, so Nearby stays empty there and the `Self` softkey has no observable
  effect. Separate follow-up.

  *Corrected during implementation:* this section originally claimed
  `hal_sim.c` "synthesises a fake `JEN` neighbour every 4 s in OG mode that no
  real board can produce". **That was wrong**, inherited from a stale review
  note. `hal_sim.c:100` already guards the fake behind
  `#if defined(WILIDORO_BOARD_OG)` / `#else` — the JEN neighbour is FW2-only
  and never appeared in OG mode. `README.md:116`'s claim that beacon reception
  stays off is what does become stale once this lands, and updating it remains
  a follow-up.
- **The softkey label clipping.** `Resume` and `Dismiss` overflow their 60 px
  buttons at 320×240 — measured, unfixed, unrelated to the radio.
- **Refreshing the og-port design's Status paragraph**, which still describes
  OG-C as simulator-only.
- **Any second band, and any expander traffic.** 433.92 MHz only.
- **Changing FW2 in any way.** `beacon_ook_encode`/`decode`, `hal_target.c` and
  the FW2 build are untouched. This plan adds files and edits OG-only paths.
- **Per-frame RSSI on `0x41`.** Nearby has no use for it; the self-test's
  numbers are what answer the RF question.
