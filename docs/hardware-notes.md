# Wilidoro Hardware Notes

Bench-discovered tuning defaults for the FreeWili 2 hardware. The `wilibsp`
BSP is a pinned upstream git submodule, so project-specific notes live here
rather than inside it (see the note at the bottom).

## WS2812 LEDs — brightness

The 16 WS2812 LEDs are **extremely** bright — full strength is uncomfortable
indoors. A comfortable indoor default is a brightness **ceiling of ~40/255**
(about 1/6 of full).

Wilidoro caps the auto-dim-derived LED brightness at `LED_BRIGHT_MAX = 40` in
`src/app/app.c`; the OPT4001 auto-dim path (`core/dimming`) still scales it
*lower* than that in a dim room, so 40 is the bright-room maximum, not a fixed
value. Verified comfortable on real hardware, 2026-07-25.

If you drive the strip directly via the BSP (`ws2812_set_brightness(level)`),
~40 is a good default `level`; the BSP's own `hello_display` uses 64, which is
already on the bright side for close-up desk use.

---

**Why this note isn't in the BSP:** `wilibsp/` is a git submodule
(`github.com/freewili/wilibsp`). To land a note *inside* the BSP itself (e.g.
`wilibsp/docs/drivers/leds.md` or the `ws2812_driver.h` header), it has to go
upstream via a fork + pull request — a local edit to the submodule isn't
captured by this repo and would break the pinned commit reference for other
clones. Ask if you'd like an upstream PR opened for it.
