#ifndef WILIDORO_HAL_H
#define WILIDORO_HAL_H
#include <stdint.h>
#include <stdbool.h>
#include "beacon.h"

/* Millisecond clock (device: to_ms_since_boot; sim: SDL_GetTicks). */
uint32_t hal_now_ms(void);

/* Physical buttons: bitmask of currently-pressed keys. */
typedef enum { HAL_BTN_A=1, HAL_BTN_B=2, HAL_BTN_X=4, HAL_BTN_Y=8, HAL_BTN_DPAD=16 } hal_btn_t;
uint32_t hal_buttons(void);

/* 16 LEDs. */
void hal_led_set(int i, uint8_t r, uint8_t g, uint8_t b);
void hal_led_brightness(uint8_t level);   /* 0..255 */
void hal_led_show(void);

/* Audio: enqueue a synthesized tone (freq Hz, duration ms, 0..255 amplitude). */
void hal_tone(uint16_t hz, uint16_t ms, uint8_t amp);
void hal_audio_idle(void);   /* power the codec down when nothing is playing */

/* Backlight 0..100 (device: GPIO25 PWM; sim: no-op or window tint). */
void hal_backlight(uint8_t pct);

/* Sensors. */
bool  hal_imu(float *ax, float *ay, float *az);   /* g; false if unavailable */
bool  hal_lux(float *lux);                          /* false if unavailable */

/* Radio beacon. */
void hal_beacon_tx(const uint8_t wire[BEACON_WIRE_LEN]);
bool hal_beacon_rx(uint8_t wire[BEACON_WIRE_LEN]);  /* true if a valid frame was captured */

/* Feature availability flags, set at init (crossed-out icons on the timer face). */
typedef struct { bool radio, imu, light, audio; } hal_caps_t;
hal_caps_t hal_caps(void);
#endif
