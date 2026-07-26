// src/app/dvi_view.c -- the DVI big-room focus view. Pure: no LVGL, no hal, no
// wilibsp, so it is host-unit-tested.
//
// Everything is drawn from clipped rectangle fills: the countdown uses
// seven-segment digits and the state word a 14-glyph 5x7 font. That avoids
// pulling in a font library and keeps every write trivially bounded -- which
// matters because on the device the bytes past `w` in each row are HSTX scanout
// COMMANDS, not pixels. fill_rect() is the single choke point that enforces it.
#include "dvi_view.h"
#include <stddef.h>

/* Palette rows are keyed by theme index the same way led_pattern.c and theme.c
   are (0=neon, 1=arcade, 2=flip). That correspondence is convention, not
   enforced -- keep the three in step by hand. */
typedef struct { uint8_t r, g, b; } dvi_rgb_t;
typedef struct { dvi_rgb_t bg, focus, brk, dim; } dvi_palette_t;

static const dvi_palette_t PALETTES[3] = {
    /* neon   */ { {12,12,18},  {255,91,69},  {45,212,191},  {107,124,147} },
    /* arcade */ { {16,16,32},  {255,71,87},  {46,213,115},  {90,90,120}   },
    /* flip   */ { {28,24,20},  {200,120,60}, {138,125,107}, {92,84,74}    },
};

static inline uint16_t rgb565(dvi_rgb_t c) {
    return (uint16_t)(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3));
}

/* The ONLY writer. Clips to the visible region so nothing can reach the HSTX
   command words that live past `w` in every row. */
static void fill_rect(const dvi_surface_t *s, int x, int y, int w, int h, uint16_t c) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= s->w || y >= s->h) return;
    if (x + w > s->w) w = s->w - x;
    if (y + h > s->h) h = s->h - y;
    if (w <= 0 || h <= 0) return;
    for (int r = 0; r < h; r++) {
        uint16_t *row = s->base + (size_t)(y + r) * (size_t)s->stride + x;
        for (int i = 0; i < w; i++) row[i] = c;
    }
}

/* Seven-segment: bit0=a(top) 1=b(upper right) 2=c(lower right) 3=d(bottom)
   4=e(lower left) 5=f(upper left) 6=g(middle). */
static const uint8_t SEG[10] = { 0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F };

static void draw_digit(const dvi_surface_t *s, int x, int y, int w, int h, int t,
                       int d, uint16_t c) {
    if (d < 0 || d > 9) return;
    uint8_t m = SEG[d];
    int hh = h / 2;
    if (m & 0x01) fill_rect(s, x + t,     y,             w - 2*t, t,      c);
    if (m & 0x02) fill_rect(s, x + w - t, y + t,         t,       hh - t, c);
    if (m & 0x04) fill_rect(s, x + w - t, y + hh,        t,       hh - t, c);
    if (m & 0x08) fill_rect(s, x + t,     y + h - t,     w - 2*t, t,      c);
    if (m & 0x10) fill_rect(s, x,         y + hh,        t,       hh - t, c);
    if (m & 0x20) fill_rect(s, x,         y + t,         t,       hh - t, c);
    if (m & 0x40) fill_rect(s, x + t,     y + hh - t/2,  w - 2*t, t,      c);
}

/* 5x7 glyphs, column-major, bit0 = top row. Only the letters the five state
   words need: FOCUS BREAK PAUSED READY DONE. */
static const char GLYPH_CH[] = "ABCDEFKNOPRSUY";
static const uint8_t GLYPH_COL[14][5] = {
    {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */
    {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */
    {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x09,0x01}, /* F */
    {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */
    {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x07,0x08,0x70,0x08,0x07}, /* Y */
};

static void draw_text(const dvi_surface_t *s, int x, int y, int scale,
                      const char *str, uint16_t c) {
    for (const char *p = str; *p; p++) {
        for (int i = 0; i < 14; i++) {
            if (GLYPH_CH[i] != *p) continue;
            for (int col = 0; col < 5; col++)
                for (int row = 0; row < 7; row++)
                    if (GLYPH_COL[i][col] & (1u << row))
                        fill_rect(s, x + col*scale, y + row*scale, scale, scale, c);
            break;
        }
        x += 6 * scale;   /* 5 columns + 1 blank */
    }
}

/* Layout for a 480x240 region. Countdown spans 36 + 408 = 444, leaving a
   symmetric 36 px margin either side. */
#define MARGIN_X    36
#define STATE_Y     18
#define STATE_SCALE  4
#define DIG_Y       68
#define DIG_W       84
#define DIG_H      120
#define DIG_T       16
#define DIG_GAP     12
#define COLON_W     24
#define DOT_Y      208
#define DOT_S       18
#define DOT_GAP     12

static const char *state_word(const timer_view_t *v) {
    if (v->alarm)  return "DONE";
    if (v->idle)   return "READY";
    if (v->paused) return "PAUSED";
    return v->break_phase ? "BREAK" : "FOCUS";
}

void dvi_view_render(uint8_t theme_idx, const timer_view_t *v, const dvi_surface_t *s) {
    const dvi_palette_t *p = &PALETTES[theme_idx < 3 ? theme_idx : 0];
    uint16_t bg  = rgb565(p->bg);
    uint16_t fg  = rgb565(v->break_phase ? p->brk : p->focus);
    uint16_t dim = rgb565(p->dim);

    fill_rect(s, 0, 0, s->w, s->h, bg);

    const char *word = state_word(v);
    int n_ch = 0; while (word[n_ch]) n_ch++;
    draw_text(s, (s->w - n_ch * 6 * STATE_SCALE) / 2, STATE_Y, STATE_SCALE, word, fg);

    /* Round UP so a running timer shows 25:00 on its first frame, not 24:59. */
    uint32_t secs = (v->rem_ms + 999u) / 1000u;
    unsigned mm = (unsigned)(secs / 60u), ss = (unsigned)(secs % 60u);
    if (mm > 99u) { mm = 99u; ss = 59u; }

    int x = MARGIN_X;
    draw_digit(s, x, DIG_Y, DIG_W, DIG_H, DIG_T, (int)(mm / 10u), fg); x += DIG_W + DIG_GAP;
    draw_digit(s, x, DIG_Y, DIG_W, DIG_H, DIG_T, (int)(mm % 10u), fg); x += DIG_W + DIG_GAP;
    fill_rect(s, x + COLON_W/2 - DIG_T/2, DIG_Y + DIG_H/3,     DIG_T, DIG_T, fg);
    fill_rect(s, x + COLON_W/2 - DIG_T/2, DIG_Y + 2*DIG_H/3,   DIG_T, DIG_T, fg);
    x += COLON_W + DIG_GAP;
    draw_digit(s, x, DIG_Y, DIG_W, DIG_H, DIG_T, (int)(ss / 10u), fg); x += DIG_W + DIG_GAP;
    draw_digit(s, x, DIG_Y, DIG_W, DIG_H, DIG_T, (int)(ss % 10u), fg);

    unsigned n = v->session_n ? v->session_n : 1u;
    if (n > 8u) n = 8u;
    int total_w = (int)n * DOT_S + ((int)n - 1) * DOT_GAP;
    int dx = (s->w - total_w) / 2;
    for (unsigned i = 0; i < n; i++)
        fill_rect(s, dx + (int)i * (DOT_S + DOT_GAP), DOT_Y, DOT_S, DOT_S,
                  i < v->session_idx ? fg : dim);
}

void dvi_dirty_reset(dvi_dirty_t *d) { d->sig = 0; d->valid = false; }

bool dvi_view_dirty(dvi_dirty_t *d, uint8_t theme_idx, const timer_view_t *v) {
    uint32_t secs = (v->rem_ms + 999u) / 1000u;
    if (secs > 0x3FFFFFu) secs = 0x3FFFFFu;
    uint32_t sig = secs
                 | ((uint32_t)(theme_idx & 3u)            << 22)
                 | ((uint32_t)(v->idle        ? 1u : 0u)  << 24)
                 | ((uint32_t)(v->paused      ? 1u : 0u)  << 25)
                 | ((uint32_t)(v->alarm       ? 1u : 0u)  << 26)
                 | ((uint32_t)(v->break_phase ? 1u : 0u)  << 27)
                 | ((uint32_t)((v->session_idx * 9u + v->session_n) & 15u) << 28);
    if (d->valid && d->sig == sig) return false;
    d->sig = sig; d->valid = true;
    return true;
}
