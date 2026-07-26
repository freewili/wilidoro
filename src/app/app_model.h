#ifndef WILIDORO_APP_MODEL_H
#define WILIDORO_APP_MODEL_H
#include <stdint.h>
#include <stdbool.h>
#include "beacon.h"

#define APP_NAME_LEN    BEACON_NAME_LEN   /* 8 */
#define NEIGHBOR_MAX    8
#define NEIGHBOR_TTL_MS 60000u

typedef struct {
    uint16_t focus_min, short_min, long_min, long_every;
    uint8_t  volume;       /* 0..100 */
    bool     focus_tick;
    bool     beacon_on;
    char     name[APP_NAME_LEN];  /* space-padded ASCII, not NUL-terminated */
    uint8_t  theme;        /* 0..2; activated in Plan B2 */
    bool     dvi_on;       /* big-room DVI display */
    bool     tilt_pause;   /* IMU: lift or tilt the board to pause a focus session */
} app_settings_t;

void app_settings_defaults(app_settings_t *s);
void app_settings_adjust_focus(app_settings_t *s, int delta);   /* clamp 5..60, step 5 */
void app_settings_adjust_short(app_settings_t *s, int delta);   /* clamp 1..30, step 1 */
void app_settings_adjust_long(app_settings_t *s, int delta);    /* clamp 5..60, step 5 */
void app_settings_adjust_volume(app_settings_t *s, int delta);  /* clamp 0..100, step 10 */
void app_settings_cycle_theme(app_settings_t *s);               /* 0->1->2->0 */

typedef struct {
    char           name[APP_NAME_LEN];
    beacon_state_t state;
    uint8_t        minutes_left;
    uint8_t        completed;
    uint32_t       last_seen_ms;
    bool           used;
} neighbor_t;

typedef struct { neighbor_t items[NEIGHBOR_MAX]; } neighbor_table_t;

void neighbor_table_init(neighbor_table_t *t);
void neighbor_upsert(neighbor_table_t *t, const beacon_msg_t *m, uint32_t now_ms);
void neighbor_expire(neighbor_table_t *t, uint32_t now_ms);
int  neighbor_count(const neighbor_table_t *t);
#endif
