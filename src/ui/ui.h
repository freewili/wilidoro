#ifndef WILIDORO_UI_H
#define WILIDORO_UI_H
#include "lvgl.h"
/* Panel geometry and the type scale that follows from it. The FreeWili 2's
   ST7796 is 480x320; the OG's ST7789 is 320x240. Screens and themes lay out
   against these rather than literals, so one set of sources serves both. */
#if defined(WILIDORO_BOARD_OG)
  #define UI_W               320
  #define UI_H               240
  #define UI_FONT_BIG        (&lv_font_montserrat_40)
  #define UI_SOFTKEY_H       28
  #define UI_SOFTKEY_BTN_W   60
  #define UI_SOFTKEY_BTN_H   22
  #define UI_ARCADE_BAR_H    16
  #define UI_FLIP_CARD       100
  #define UI_ROW_H           34
  #define UI_STEP_BTN_W      28
  #define UI_STEP_BTN_H      24
  #define UI_SET_LABEL_W     132
  #define UI_SET_VAL_W       66
#else
  #define UI_W               480
  #define UI_H               320
  #define UI_FONT_BIG        (&lv_font_montserrat_48)
  #define UI_SOFTKEY_H       34
  #define UI_SOFTKEY_BTN_W   92
  #define UI_SOFTKEY_BTN_H   28
  #define UI_ARCADE_BAR_H    22
  #define UI_FLIP_CARD       150
  #define UI_ROW_H           44
  #define UI_STEP_BTN_W      34
  #define UI_STEP_BTN_H      30
  #define UI_SET_LABEL_W     230
  #define UI_SET_VAL_W       90
#endif
/* The face is everything above the softkey bar. */
#define UI_FACE_H           (UI_H - UI_SOFTKEY_H)
/* Health-bar width: 400 on FW2 (480-80), 240 on the OG (320-80), matching
   the literal each build had before. */
#define UI_ARCADE_BAR_W      (UI_W - 80)
/* Flip-clock session-bar width: 300 on FW2, 140 on the OG. */
#define UI_FLIP_BAR_W         (UI_W - 180)
/* Flip card centre offset from the face centre: 84 on FW2, 59 on the OG,
   matching the literal each build had before. */
#define UI_FLIP_CARD_X        (UI_FLIP_CARD/2 + 9)
/* Flip session-bar y-offset from the face centre: 84 on FW2, 47 on the OG,
   matching the literal each build had before. */
#define UI_FLIP_BAR_Y         (UI_FACE_H/2 - 59)
/* Flip status-label y-offset from the face centre: 108 on FW2, 71 on the OG,
   matching the literal each build had before. */
#define UI_FLIP_STATUS_Y      (UI_FACE_H/2 - 35)
/* Settings/Nearby list width, inset 2px each side from the panel: 476 on FW2,
   316 on the OG, matching the literal each build had before. */
#define UI_LIST_W             (UI_W - 4)
/* Settings/Nearby list height, below the 34px title band: 236 on FW2, 162 on
   the OG, matching the literal each build had before. */
#define UI_LIST_H             (UI_FACE_H - 50)
/* Settings/Nearby row width, inset 10px each side: 460 on FW2, 300 on the OG,
   matching the literal each build had before. */
#define UI_ROW_W              (UI_W - 20)
/* Nearby row height is 44 on BOTH boards. It cannot shrink to UI_ROW_H (34 on
   the OG) like screen_settings.c's rows do: each Nearby row stacks two
   montserrat_16 labels (name at TOP_LEFT y=+4, sub at BOTTOM_LEFT y=-4, each
   19px tall), which need ~42px clear or they overlap. */
#define UI_NEARBY_ROW_H       44
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
