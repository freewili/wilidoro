#ifndef WILIDORO_UI_H
#define WILIDORO_UI_H
#include "lvgl.h"
/* neutral palette (Plan B2 themes override per-screen) */
#define UI_BG      0x0C0C12
#define UI_PANEL   0x141b26
#define UI_TEXT    0xDDE6F2
#define UI_MUTED   0x6B7C93
#define UI_ACCENT  0xFF5B45
#define UI_COOL    0x2DD4BF
/* Build a 5-cell softkey bar pinned to the screen bottom; labels[i]==NULL -> blank cell.
   Clicks invoke `cb(col)`. Returns the bar container; call ui_softkey_set_labels to relabel. */
typedef void (*ui_softkey_cb_t)(int col);
lv_obj_t *ui_softkey_bar(lv_obj_t *parent, ui_softkey_cb_t cb);
void      ui_softkey_set_labels(lv_obj_t *bar, const char *labels[5]);
lv_obj_t *ui_screen(void);   /* new full screen obj with neutral bg */
#endif
