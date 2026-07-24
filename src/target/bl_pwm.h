#ifndef WILIDORO_BL_PWM_H
#define WILIDORO_BL_PWM_H
#include <stdint.h>
void bl_pwm_init(void);
void bl_pwm_set(uint8_t pct);   /* 0..100 */
#endif
