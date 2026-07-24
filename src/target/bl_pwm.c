#include "bl_pwm.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#define BL_PIN 25
static uint s_slice;
void bl_pwm_init(void) {
    gpio_set_function(BL_PIN, GPIO_FUNC_PWM);
    s_slice = pwm_gpio_to_slice_num(BL_PIN);
    pwm_set_wrap(s_slice, 1000);
    pwm_set_enabled(s_slice, true);
    bl_pwm_set(100);
}
void bl_pwm_set(uint8_t pct) {
    if (pct > 100) pct = 100;
    pwm_set_gpio_level(BL_PIN, (uint16_t)(pct * 10));
}
