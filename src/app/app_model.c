#include "app_model.h"
#include <string.h>

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void app_settings_defaults(app_settings_t *s) {
    s->focus_min = 25; s->short_min = 5; s->long_min = 15; s->long_every = 4;
    s->volume = 70; s->focus_tick = false; s->beacon_on = true; s->theme = 0;
    memcpy(s->name, "WILI    ", APP_NAME_LEN);
}
void app_settings_adjust_focus(app_settings_t *s, int delta) {
    s->focus_min = (uint16_t)clampi((int)s->focus_min + delta * 5, 5, 60);
}
void app_settings_adjust_short(app_settings_t *s, int delta) {
    s->short_min = (uint16_t)clampi((int)s->short_min + delta, 1, 30);
}
void app_settings_adjust_long(app_settings_t *s, int delta) {
    s->long_min = (uint16_t)clampi((int)s->long_min + delta * 5, 5, 60);
}
void app_settings_adjust_volume(app_settings_t *s, int delta) {
    s->volume = (uint8_t)clampi((int)s->volume + delta * 10, 0, 100);
}
void app_settings_cycle_theme(app_settings_t *s) { s->theme = (uint8_t)((s->theme + 1) % 3); }
