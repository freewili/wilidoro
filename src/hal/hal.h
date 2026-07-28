#ifndef WILIDORO_HAL_H
#define WILIDORO_HAL_H
#include <stdint.h>
#include <stdbool.h>
#include "beacon.h"

/* Called once at startup (before app_init) and once per main-loop iteration. */
void hal_init(void);
void hal_pump(void);           /* drain hardware input each loop iteration (device: uartkbd_task) */

uint32_t hal_now_ms(void);

/* FreeWili 2 physical buttons (14-button coprocessor). Event queue of PRESS edges. */
typedef enum {
    HAL_BTN_GREY = 0, HAL_BTN_YELLOW, HAL_BTN_GREEN, HAL_BTN_BLUE, HAL_BTN_RED, /* 5 softkeys below screen */
    HAL_BTN_UP, HAL_BTN_DOWN, HAL_BTN_LEFT, HAL_BTN_RIGHT, HAL_BTN_CENTER,      /* D-pad */
    HAL_BTN_HOME, HAL_BTN_OK, HAL_BTN_CANCEL, HAL_BTN_PAGE,
    HAL_BTN_COUNT
} hal_btn_t;
bool hal_next_button(hal_btn_t *out);  /* dequeue next press edge; false if none */

/* True while a hardware power-off countdown is running (FreeWili OG: red held).
   The BSP paints that countdown on the LED bar itself, so the app must skip
   its own LED writes while this is true. Always false on FreeWili 2. */
bool hal_power_armed(void);

/* LEDs. Count is board-dependent: 16 on FreeWili 2, 7 on the OG. */
int  hal_led_count(void);
void hal_led_set(int i, uint8_t r, uint8_t g, uint8_t b);
void hal_led_brightness(uint8_t level);   /* 0..255 */
void hal_led_show(void);

/* Audio (Plan C implements; Plan B stubs on device). */
void hal_tone(uint16_t hz, uint16_t ms, uint8_t amp);
void hal_audio_idle(void);

/* Backlight 0..100. */
void hal_backlight(uint8_t pct);

/* DVI big-room display (Plan DVI). The surface is a strided native-endian RGB565
   framebuffer: row y starts at base + (size_t)y*stride and is w pixels wide.
   stride MAY EXCEED w -- the slack holds HSTX scanout commands on the device.
   Geometry must match DVI_VIEW_W/DVI_VIEW_H in src/app/dvi_view.h and the
   HSTX_VID_*_MAX compile definitions in the root CMakeLists.txt. */
typedef struct { uint16_t *base; int stride, w, h; } hal_dvi_surface_t;
bool hal_dvi_surface(hal_dvi_surface_t *s);
void hal_dvi_enable(bool on);

/* Sensors (Plan C; Plan B device returns false). */
bool hal_imu(float *ax, float *ay, float *az);
bool hal_lux(float *lux);

/* Radio beacon (Plan C; Plan B device stubs; sim fakes). */
void hal_beacon_tx(const uint8_t wire[BEACON_WIRE_LEN]);
bool hal_beacon_rx(uint8_t wire[BEACON_WIRE_LEN]);

/* Which features are live this build (crossed-out icons on the timer face). */
typedef struct { bool radio, imu, light, audio, buttons, leds, dvi; } hal_caps_t;
hal_caps_t hal_caps(void);
#endif
