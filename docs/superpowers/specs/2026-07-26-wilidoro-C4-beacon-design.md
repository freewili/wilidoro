# Wilidoro Plan C4 — CC1101 focus beacon

*Design approved 2026-07-26. The last piece of the original
`2026-07-24-wilidoro-pomodoro-design.md` spec.*

Wilidoro broadcasts its focus state over the CC1101 sub-GHz radio and listens for
other wilidoros, so a room of them can see each other's pomodoros on the Nearby
screen.

The pure halves already exist and are host-tested from Plan A: `core/beacon.c`
(packet, CRC16, OOK Manchester codec) and `app/app_model.c`'s neighbour table,
plus the Nearby screen from Plan B1. C4 is the hardware half, and one missing
piece of pure logic that turns out to be the real work.

## 1. The decoder constraint that shapes everything

`beacon_ook_decode` is considerably stricter than the original spec implied. Read
it before designing anything around it:

- It assumes `durs[0]` is a **HIGH** run (`uint8_t level = 1`). There is no
  framing search and no polarity detection.
- It **rejects any run longer than 4 half-bits** (`if (units == 0 || units > 4)
  return false`). A capture stream containing an idle gap can therefore *never*
  decode — the gap alone fails it.
- It requires exactly `(BEACON_PREAMBLE_BITS + BEACON_WIRE_LEN * 8) * 2`
  half-bits and verifies the preamble at a fixed offset.

**Consequence:** raw `gdo_capture_drain()` output cannot be handed to it. Something
must first cut the stream into candidate frames. That something is pure logic, and
it is the only genuinely new logic in C4.

### The merged trailing half-bit

`ook_tx_send` leaves the pin low at the end of a burst (carrier off). Manchester
encodes a `1` as half-bits `{1,0}` and a `0` as `{0,1}`. So:

- **Last data bit `1`** → the frame ends on a low half-bit, which **merges into
  the idle gap** and is lost from the capture. The segment is one half-bit short
  and `beacon_ook_decode` rejects it for `nhb < need`.
- **Last data bit `0`** → the frame ends high, the gap terminates it, and the
  capture is complete.

Both cases leave an **odd** number of runs in the captured segment, so run-count
parity cannot distinguish them. The framer must attempt the segment as captured
and, on failure, retry with one synthetic low half-bit appended. Two attempts,
both cheap.

## 2. New pure module — `src/core/beacon_rx.{h,c}`

A streaming burst framer. No LVGL, no hardware, host-tested.

```c
/* Longer than any legal run: beacon_ook_decode rejects runs above 4 half-bits,
   so 5 is the smallest unambiguous "this is the inter-frame gap" threshold. */
#define BEACON_GAP_US (BEACON_HALFBIT_US * 5u)

typedef struct {
    uint32_t durs[BEACON_MAX_DURS];
    size_t   n;
    bool     armed;    /* true once a gap has established high-run polarity */
} beacon_rx_t;

void beacon_rx_init(beacon_rx_t *r);
bool beacon_rx_push(beacon_rx_t *r, uint32_t dur_us, uint8_t out[BEACON_WIRE_LEN]);
```

`beacon_rx_push` consumes one duration at a time and returns `true` exactly when a
frame decodes, writing the 16-byte wire frame to `out`.

Behaviour:

- A run `>= BEACON_GAP_US` **closes** the current segment: attempt
  `beacon_ook_decode` on it, then (on failure, and only if there is room) retry
  with one appended `BEACON_HALFBIT_US` low run. Then reset `n` and set
  `armed = true`. An empty segment (`n == 0`, the common case for back-to-back
  idle gaps) is skipped without attempting a decode.
- **Polarity is derived, not guessed.** Idle is carrier-off, i.e. low. The run
  *following* a gap is therefore high — exactly what `beacon_ook_decode` assumes.
  This is why nothing is appended until `armed` is set: durations arriving before
  the first gap have unknown polarity and are discarded.
- A run `< BEACON_GAP_US` while `armed` is appended to the segment.
- Overflow past `BEACON_MAX_DURS` discards the segment (`n = 0`, stay `armed`) —
  noise must not wedge the framer permanently.

Frames that survive framing are still checked by `beacon_unpack`'s magic, version
and CRC16, which is what rejects noise that happens to frame plausibly.

## 2b. Building the outgoing message — `app_beacon_msg`

Nothing in the codebase maps app state to a `beacon_msg_t` today; the type is only
ever consumed, on the receive side. TX needs that mapping, and it is pure logic, so
it goes in `src/app/app_model.c` beside `neighbor_upsert` — which already speaks
both `beacon_msg_t` and (since Plan C3's gate predicates) `pm_state_t`:

```c
void app_beacon_msg(const app_settings_t *s, const pomodoro_t *p,
                    uint32_t now_ms, beacon_msg_t *out);
```

- `name` ← `s->name` verbatim (already space-padded, not NUL-terminated, exactly
  `BEACON_NAME_LEN`).
- `state` ← `PM_FOCUS` → `BST_FOCUS`; `PM_BREAK_SHORT` / `PM_BREAK_LONG` →
  `BST_BREAK`; **everything else, including `PM_PAUSED` and `PM_ALARM`, →
  `BST_IDLE`.** A paused session is not focusing, and broadcasting otherwise would
  make a neighbour's "focusing, 12 min left" freeze at a stale figure. This is a
  deliberate choice, not an oversight.
- `minutes_left` ← `pomodoro_remaining_ms(p, now_ms) / 60000`, clamped to 255.
- `completed` ← `p->stats.completed`, clamped to 255.

Both clamps matter: the wire fields are single bytes, and `completed` accumulates
without bound across a long session.

## 3. HAL — the seam already exists

`hal_beacon_tx(const uint8_t wire[BEACON_WIRE_LEN])` and
`bool hal_beacon_rx(uint8_t wire[BEACON_WIRE_LEN])` are already declared in
`src/hal/hal.h` (lines 48–49). **No header change.** The HAL may call `core/`
freely, so the framer lives where CTest can reach it while the seam stays fixed.

### Device (`src/hal/hal_target.c`)

**Init**, after the existing sensor bring-up:

```c
ioexp_antenna(ANT_CC1101_433);   /* route a CC1101 antenna before any SPI traffic */
s_radio = cc1101_init();          /* probes PARTNUM/VERSION; DIAGs them itself */
if (s_radio) {
    gdo_capture_init();
    gdo_capture_start();
    cc1101_monitor_rx(BEACON_HZ, CC1101_MOD_ASK_OOK);   /* GDO0 = demodulated data */
}
```

`hal_caps().radio` reflects `s_radio`. `BEACON_HZ` is a `#define` of `433920000u`
local to `src/hal/hal_target.c` — it is a hardware-routing fact, not app policy, so
it does not belong in `core/beacon.h` alongside the wire format.

**`hal_beacon_tx`:** `beacon_ook_encode` → `cc1101_tx_ook_start(BEACON_HZ)` →
`ook_tx_send(durs, n, start_level)` → `cc1101_tx_ook_stop()` →
`gdo_capture_attach_pin()` (undo the SIO takeover) → `cc1101_monitor_rx(...)` to
return to listening. A no-op when `!s_radio`.

**`hal_beacon_rx`:** `gdo_capture_drain()` into a local buffer, push each duration
through a file-static `beacon_rx_t`, and return `true` on the first frame decoded.
The framer's segment state persists across calls, so a frame spanning several polls
is assembled correctly.

Returning early **discards the rest of that drain buffer, and that is deliberate** —
do not "fix" it with a pending-duration queue. A frame takes 136 ms to transmit
while the drain runs every 100 ms, so at most one frame boundary can fall inside a
single drain; two complete frames in one buffer is impossible. The worst case is
losing the leading durations of a following frame, which the next gap re-arms out
of anyway. A queue would add untestable HAL state to solve a case that cannot occur.

### Simulator (`src/hal/hal_sim.c`)

Unchanged. It already fakes a neighbour every 4 s, which is what lets the Nearby
screen be developed without hardware.

### Why no async flush migration

The original spec said C4 "requires migrating the LVGL flush from the blocking
`st7796_blit_rect` to `st7796_flush_async` plus SPI1 arbitration." **That is not
required, and this design does not do it.** Reasons, in order of importance:

1. `gdo_capture` is explicitly off-bus: *"Free-running once started; never touches
   SPI1 / PIN_LCD_CS."*
2. `ook_tx_send` bit-bangs GDO0 (GPIO32). The long 136 ms blocking burst touches
   no SPI at all.
3. The CC1101's only SPI traffic is short register bursts, and the driver already
   takes the bus through the BSP's `spi_bus_acquire_cc1101()` / `_release_cc1101()`
   arbiter, which also handles the GPIO8 mux (LCD DC ↔ CC1101 MISO).
4. Wilidoro's flush is blocking and every caller runs from an LVGL timer on core 0,
   so SPI1 has exactly one owner at any instant by construction.

Rewriting a hardware-verified display path for no benefit would be the single
riskiest change available. `st7796_flush_busy()` exists for a *concurrent* owner;
we do not have one.

## 4. Cadence and the 136 ms burst

A frame is `(BEACON_PREAMBLE_BITS + BEACON_WIRE_LEN * 8) = 136` bits, each two
half-bits of `BEACON_HALFBIT_US = 500`, so **136 ms of blocking GPIO toggling**
per transmit. Core 0 is frozen for the whole burst.

- **TX every 20 s.** `NEIGHBOR_TTL_MS` is 60 s, so a listener gets three chances
  before an entry expires.
- **Gated on `settings.beacon_on && !sound_active(&s_app.sound)`.** The audio gate
  is the entire mitigation: a 136 ms stall would stretch a chime audibly, but the
  countdown only updates once a second so the visual hitch is near-invisible.
  `sound_active()` already exists in `src/app/sound.h` — no new accessor.
- **RX drains in `sensor_cb`** (the existing 100 ms hardware-polling timer). At a
  500 µs half-bit the line carries at most ~2000 edges/s, so ≤ ~200 per drain.
- Deliberately **not** using core 1. It is free and would keep the UI perfectly
  smooth, but it would make SPI1 genuinely concurrent, require multicore plumbing,
  and stack new risk on top of the unproven three-PIO coexistence below. If the
  hitch proves visible in practice, core 1 is the upgrade path.

## 5. Verification, and the honest limits

**Over-the-air RX has never been demonstrated on this hardware by anyone.** The
BSP's own findings doc is explicit that `hello_cc1101`'s phase 3 is a *same-pad
plumbing test*, not an OTA test, and that proving a real demod path "needs an
external 433 MHz transmitter." This design does not pretend otherwise.

### The loopback self-test (one board, no second radio)

`hello_cc1101` starts `gdo_capture` *before* transmitting and drains exactly as
many edges as it sent (`sent=24 pulses, drained=24 edges`). So PIO2 samples the
pad even while the MCU drives it as SIO. Wilidoro can therefore transmit its own
beacon and receive it back through the pad, exercising the entire chain:

```
beacon_pack → beacon_ook_encode → ook_tx_send → PIO2/DMA capture
           → beacon_rx framer → beacon_ook_decode → beacon_unpack
```

Everything except the RF air path and the CC1101's demodulator. Run once at boot
when the radio comes up, result logged over RTT (`beacon: loopback ok` /
`FAILED`). No UI, no new interaction.

It **fails closed**: if the pad-sampling assumption turns out not to hold, the
decode simply fails and the log says so. It cannot produce a false pass.

### Conditional two-device test

A **FreeWili One** is also on the bench. `wilibsp` only supports `freewili2`, so
that board cannot run wilidoro firmware. Whether it can serve as a transmitter
depends on facts not determinable from this repo: whether it carries a 433 MHz
CC1101 and whether its own tooling can send raw OOK. If it can, the real
two-device test becomes available — one board transmitting, the other listing it
on Nearby. Treated as a conditional step, not a dependency.

## 6. Risks

- **Three-PIO coexistence is unproven.** Wilidoro already runs PIO0 (I2S audio)
  and PIO1 (WS2812); radio capture is PIO2. The BSP records this as
  "architecturally sound but was not co-exercised on silicon." C4 is the first
  time all three run together. **If audio or LEDs break when the radio starts,
  this is the first suspect** — bisect by skipping `gdo_capture_init()`.
- **OOK RX noise.** With no signal present the CC1101's AGC produces continuous
  spurious edges, so the framer will see constant garbage. Rejection rests on
  magic + version + CRC16. `BEACON_GAP_US` is the tuning knob if it thrashes.
- **Ring overflow.** `gdo_capture`'s DMA ring is finite and undocumented in size.
  Draining at 100 ms should stay ahead of it; a burst of noise that overruns shows
  up as discarded segments, not as a wedge.
- **Antenna routing.** `ioexp_antenna()` talks to the PCAL6524 over **I2C1** — the
  same bus `sensor_cb` owns. It is called once during `hal_init`, before any timer
  exists, so there is no contention, but it must stay in init and never move into
  a timer callback.

## 7. Testing

**Host** — `app_beacon_msg` is covered in the existing `test_app_model` binary:
each `pm_state_t` maps to the right `beacon_state_t` (with `PM_PAUSED` and
`PM_ALARM` both landing on `BST_IDLE`), the name copies through unchanged, and both
byte clamps hold when remaining minutes or completed counts exceed 255.

**Host** — `tests/test_beacon_rx.c`, a new binary (suite **10 → 11**):

- Round trip: `beacon_pack` → `beacon_ook_encode` → push each duration through the
  framer with a leading and trailing gap → the original wire frame comes back.
- The **merged trailing half-bit** case: drop the final low run (last data bit `1`)
  and confirm the retry path still decodes it. This is the subtle one.
- The clean-tail case (last data bit `0`) decodes on the first attempt.
- Durations arriving **before** any gap are discarded (polarity unknown).
- A gap splits two back-to-back frames and both decode.
- Mid-stream garbage between two good frames does not prevent the second decoding.
- Overflow past `BEACON_MAX_DURS` discards the segment and the framer recovers on
  the next frame rather than wedging.

**Device** — the boot loopback self-test over RTT, plus a checklist appended to
`docs/hardware-notes.md` covering: radio presence (`cc1101: PARTNUM/VERSION`),
audio and LEDs still working with PIO2 live, the Nearby screen with the beacon on,
and the transmit hitch not being visibly disruptive. **To be run only with the
user's explicit say-so**, per the standing hardware rule.

## 8. Out of scope

Core-1 transmit, the RSSI scan engine, `capture_store`/PSRAM clips, the LoRa
front-end, frequencies other than 433.92 MHz, any change to the LVGL flush path,
and any change to the Nearby screen's layout (it already renders the neighbour
table).
