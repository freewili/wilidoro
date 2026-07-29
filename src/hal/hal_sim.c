#include "hal.h"
#include "dvi_view.h"
#include <SDL2/SDL.h>
#include <string.h>
#include <math.h>

/* --- button event queue, fed by an SDL event watch (snoops without consuming) --- */
#define QN 32
static volatile int q_head = 0, q_tail = 0;
static hal_btn_t q_buf[QN];
static void q_push(hal_btn_t b) { int n = (q_head + 1) % QN; if (n != q_tail) { q_buf[q_head] = b; q_head = n; } }

static int map_key(SDL_Scancode sc, hal_btn_t *out) {
    switch (sc) {
        case SDL_SCANCODE_Z: *out = HAL_BTN_GREY;   return 1;
        case SDL_SCANCODE_X: *out = HAL_BTN_YELLOW; return 1;
        case SDL_SCANCODE_C: *out = HAL_BTN_GREEN;  return 1;
        case SDL_SCANCODE_V: *out = HAL_BTN_BLUE;   return 1;
        case SDL_SCANCODE_B: *out = HAL_BTN_RED;    return 1;
        case SDL_SCANCODE_UP:     *out = HAL_BTN_UP;     return 1;
        case SDL_SCANCODE_DOWN:   *out = HAL_BTN_DOWN;   return 1;
        case SDL_SCANCODE_LEFT:   *out = HAL_BTN_LEFT;   return 1;
        case SDL_SCANCODE_RIGHT:  *out = HAL_BTN_RIGHT;  return 1;
        case SDL_SCANCODE_RETURN: *out = HAL_BTN_CENTER; return 1;
        default: return 0;
    }
}
/* fake ambient lux for the auto-dim demo; [ / ] lower/raise it (not buttons, so not queued) */
static float s_lux = 300.0f;
/* fake board tilt, degrees away from face-up level; - / = tilt it, same idea.
   Stepping rather than snapping flat<->upright means the simulator actually
   exercises the hysteresis band and the hold timer in core/tilt.c. */
static int s_tilt_deg = 0;

static int SDLCALL key_watch(void *u, SDL_Event *e) {
    (void)u;
    if (e->type == SDL_KEYDOWN && e->key.repeat == 0) {
        hal_btn_t b; if (map_key(e->key.keysym.scancode, &b)) q_push(b);
        if (e->key.keysym.scancode == SDL_SCANCODE_LEFTBRACKET)  s_lux = (s_lux > 20.f) ? s_lux - 40.f : 0.f;
        if (e->key.keysym.scancode == SDL_SCANCODE_RIGHTBRACKET) s_lux = (s_lux < 960.f) ? s_lux + 40.f : 1000.f;
        if (e->key.keysym.scancode == SDL_SCANCODE_MINUS)  s_tilt_deg = (s_tilt_deg > 15) ? s_tilt_deg - 15 : 0;
        if (e->key.keysym.scancode == SDL_SCANCODE_EQUALS) s_tilt_deg = (s_tilt_deg < 75) ? s_tilt_deg + 15 : 90;
    }
    return 1; /* keep event in queue for LVGL */
}

void hal_init(void) { SDL_AddEventWatch(key_watch, NULL); }
void hal_pump(void) { /* SDL is pumped by lv_timer_handler; nothing to do */ }

uint32_t hal_now_ms(void) { return SDL_GetTicks(); }

bool hal_next_button(hal_btn_t *out) {
    if (q_tail == q_head) return false;
    *out = q_buf[q_tail]; q_tail = (q_tail + 1) % QN; return true;
}

bool hal_power_armed(void) { return false; }

/* LEDs — store state; a visible on-screen strip is a Plan C nicety, so keep it minimal here. */
static uint8_t s_led_bri = 40;
#if defined(WILIDORO_BOARD_OG)
int hal_led_count(void) { return 7; }
#else
int hal_led_count(void) { return 16; }
#endif
void hal_led_set(int i, uint8_t r, uint8_t g, uint8_t b) { (void)i;(void)r;(void)g;(void)b; }
void hal_led_brightness(uint8_t level) { s_led_bri = level; }
void hal_led_show(void) { (void)s_led_bri; }

void hal_tone(uint16_t hz, uint16_t ms, uint8_t amp) { (void)hz;(void)ms;(void)amp; }
void hal_audio_idle(void) {}

void hal_backlight(uint8_t pct) { (void)pct; }

/* Fake tilt, shared by both boards' simulators. Plan OG-B task 4 has since
   landed the real LIS3DH driver in hal_og.c (bench-measured: Z reads +1 g
   flat, falling toward 0 as the board tips up -- see hal_og.c's "WHICH AXIS
   IS FLAT" comment), so this used to be an OG-only `#else` branch with a
   hard `return false` on the OG side and a comment insisting the two stay
   in lockstep -- "update this branch and hal_og.c together ... not one
   without the other." This IS that update: az=+1 when flat, same
   convention hal_og.c's real mapping uses, so tilt.c's shared gate is
   exercised identically in both boards' simulators as on real hardware, and
   the OG simulator -- the only way to exercise tilt without a board -- can
   actually exercise it. */
bool hal_imu(float *ax, float *ay, float *az) {
    float rad = (float)s_tilt_deg * 3.14159265f / 180.0f;
    *ax = sinf(rad); *ay = 0.0f; *az = cosf(rad);
    return true;
}
#if defined(WILIDORO_BOARD_OG)
bool hal_lux(float *lux) { (void)lux; return false; }   /* no ambient sensor on the OG */
#else
bool hal_lux(float *lux) { *lux = s_lux; return true; }
#endif

/* hal_beacon_tx() is a no-op stand-in on both boards, matching hal_og.c and
   hal_target.c, so it needs no per-board split. */
void hal_beacon_tx(const uint8_t wire[BEACON_WIRE_LEN]) { (void)wire; }
#if defined(WILIDORO_BOARD_OG)
/* Plan OG-D HAS now landed on the device: hal_og.c reaches the OG's two
   CC1101s over the inter-CPU link, and Nearby populates on real hardware.
   This branch still returns false, deliberately -- the SIMULATOR models
   neither the link nor the radios, and synthesizing a fake neighbour here
   would show a populated Nearby list this build cannot actually have heard.
   That is the exact failure mode the hal_imu() stub above exists to avoid for
   tilt.

   Consequence worth knowing: in the OG simulator, Nearby's "Self" softkey
   renders and toggles but has no observable effect, because there is no echo
   to hide. Teaching the simulator to model the beacon was explicitly out of
   scope for Plan OG-D. */
bool hal_beacon_rx(uint8_t wire[BEACON_WIRE_LEN]) { (void)wire; return false; }
#else
/* Fake neighbor: emit one valid beacon frame ~every 4s so the Nearby screen has content. */
bool hal_beacon_rx(uint8_t wire[BEACON_WIRE_LEN]) {
    static uint32_t last = 0; uint32_t now = SDL_GetTicks();
    if (now - last < 4000) return false;
    last = now;
    beacon_msg_t m; memcpy(m.name, "JEN     ", BEACON_NAME_LEN);
    m.state = BST_FOCUS; m.minutes_left = (uint8_t)(12 + (now/1000) % 5); m.completed = 3;
    beacon_pack(&m, wire);
    return true;
}
#endif

/* The simulator does not model the echo, so this records nothing. See the OG
   branch above for what that means for Nearby's Self softkey. */
void hal_beacon_show_self(bool show) { (void)show; }

#if !defined(WILIDORO_BOARD_OG)
/* --- DVI: plain RAM, blitted into a second SDL window ---------------------
   The stride is deliberately WIDER than the width so the simulator exercises
   the same strided path as the device, where the slack holds HSTX commands. */
#define SIM_DVI_STRIDE (DVI_VIEW_W + 64)
static uint16_t s_dvi_px[DVI_VIEW_H * SIM_DVI_STRIDE];
static SDL_Window   *s_dvi_win;
static SDL_Renderer *s_dvi_ren;
static SDL_Texture  *s_dvi_tex;
static bool s_dvi_on = true;

bool hal_dvi_surface(hal_dvi_surface_t *s) {
    s->base = s_dvi_px; s->stride = SIM_DVI_STRIDE;
    s->w = DVI_VIEW_W;  s->h = DVI_VIEW_H;
    return true;
}
void hal_dvi_enable(bool on) { s_dvi_on = on; }

void sim_dvi_create(void) {
    s_dvi_win = SDL_CreateWindow("wilidoro DVI (640x480 region 480x240)",
                                 SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                 DVI_VIEW_W, DVI_VIEW_H, SDL_WINDOW_SHOWN);
    if (!s_dvi_win) return;
    s_dvi_ren = SDL_CreateRenderer(s_dvi_win, -1, SDL_RENDERER_ACCELERATED);
    if (!s_dvi_ren) return;
    s_dvi_tex = SDL_CreateTexture(s_dvi_ren, SDL_PIXELFORMAT_RGB565,
                                  SDL_TEXTUREACCESS_STREAMING, DVI_VIEW_W, DVI_VIEW_H);
}

void sim_dvi_present(void) {
    if (!s_dvi_ren || !s_dvi_tex) return;
    if (s_dvi_on) {
        /* SDL takes a byte pitch; our stride is in uint16 elements. */
        SDL_UpdateTexture(s_dvi_tex, NULL, s_dvi_px, SIM_DVI_STRIDE * (int)sizeof(uint16_t));
    } else {
        SDL_SetRenderDrawColor(s_dvi_ren, 0, 0, 0, 255);
    }
    SDL_RenderClear(s_dvi_ren);
    if (s_dvi_on) SDL_RenderCopy(s_dvi_ren, s_dvi_tex, NULL, NULL);
    SDL_RenderPresent(s_dvi_ren);
}

hal_caps_t hal_caps(void) { hal_caps_t c = { .radio=true,.imu=true,.light=true,.audio=true,.buttons=true,.leds=true,.dvi=true }; return c; }

#else /* WILIDORO_BOARD_OG */

/* No HSTX on the OG's RP2040 -- mirrors hal_og.c exactly. app.c still calls
   hal_dvi_enable() unconditionally from app_dvi_apply(), so it must exist,
   but there is nothing for it to do. */
bool hal_dvi_surface(hal_dvi_surface_t *s) { (void)s; return false; }
void hal_dvi_enable(bool on) { (void)on; }

/* hal_caps() matches hal_og.c's profile for the hardware this board truly
   lacks: no light sensor, no DVI. radio=false is NOT "OG-D is unbuilt" any
   more -- the real OG's beacon works; it is false because the SIMULATOR
   models neither the CC1101s nor the inter-CPU link they arrive over, so
   claiming a radio here would promise a Nearby list this build can never
   populate. imu=true, not
   false: the OG simulator's hal_imu() above always succeeds (fake tilt,
   shared with the FW2 sim), matching the FW2 sim's own always-true .imu
   just above -- reporting false here while hal_imu() actually works would
   make screen_settings.c's Tilt row show "no imu" even though the -/= keys
   genuinely pause and resume a session. Buttons/LEDs/audio are real in the
   sim, same as the board. */
hal_caps_t hal_caps(void) {
    hal_caps_t c = {0};
    c.buttons = true;
    c.leds    = true;
    c.audio   = true;
    c.imu     = true;
    c.radio   = false;
    c.light   = false;
    c.dvi     = false;
    return c;
}

#endif /* WILIDORO_BOARD_OG */
