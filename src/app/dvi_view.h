// src/app/dvi_view.h
#ifndef WILIDORO_DVI_VIEW_H
#define WILIDORO_DVI_VIEW_H
#include <stdint.h>
#include <stdbool.h>
#include "timer_view.h"

/* The stored DVI video region. Must match HSTX_VID_W_MAX/HSTX_VID_H_MAX in the
   build (see the root CMakeLists.txt) -- the layout constants assume this size. */
#define DVI_VIEW_W 480
#define DVI_VIEW_H 240

/* A strided, native-little-endian RGB565 surface. Row y starts at
   base + (size_t)y*stride and is w pixels wide.
   stride MAY EXCEED w: on the device the extra words per row are HSTX scanout
   COMMANDS. Writing into them corrupts the display program, so every write here
   is clipped to x in [0,w) and y in [0,h). */
typedef struct { uint16_t *base; int stride, w, h; } dvi_surface_t;

/* Render the whole view for `theme_idx` (0=neon, 1=arcade, 2=flip; out of range
   clamps to 0). Paints the background first, so no clearing is needed. */
void dvi_view_render(uint8_t theme_idx, const timer_view_t *v, const dvi_surface_t *s);

/* Caller-owned redraw tracking (NOT a static inside the module -- hidden state
   leaks between unit tests and makes render-skipping untestable). */
typedef struct { uint32_t sig; bool valid; } dvi_dirty_t;
void dvi_dirty_reset(dvi_dirty_t *d);
/* True when the visible output would differ from what `d` last accepted, and
   updates `d`. A freshly reset tracker always returns true. */
bool dvi_view_dirty(dvi_dirty_t *d, uint8_t theme_idx, const timer_view_t *v);
#endif
