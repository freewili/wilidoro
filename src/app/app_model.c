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

void neighbor_table_init(neighbor_table_t *t) {
    for (int i = 0; i < NEIGHBOR_MAX; i++) t->items[i].used = false;
}

int neighbor_count(const neighbor_table_t *t) {
    int n = 0;
    for (int i = 0; i < NEIGHBOR_MAX; i++) if (t->items[i].used) n++;
    return n;
}

static void copy_from_msg(neighbor_t *dst, const beacon_msg_t *m, uint32_t now_ms) {
    memcpy(dst->name, m->name, APP_NAME_LEN);
    dst->state = m->state;
    dst->minutes_left = m->minutes_left;
    dst->completed = m->completed;
    dst->last_seen_ms = now_ms;
    dst->used = true;
}

void neighbor_upsert(neighbor_table_t *t, const beacon_msg_t *m, uint32_t now_ms) {
    /* 1) update existing by name */
    for (int i = 0; i < NEIGHBOR_MAX; i++)
        if (t->items[i].used && memcmp(t->items[i].name, m->name, APP_NAME_LEN) == 0) {
            copy_from_msg(&t->items[i], m, now_ms); return;
        }
    /* 2) use a free slot */
    for (int i = 0; i < NEIGHBOR_MAX; i++)
        if (!t->items[i].used) { copy_from_msg(&t->items[i], m, now_ms); return; }
    /* 3) evict the oldest (smallest last_seen_ms) */
    int oldest = 0;
    for (int i = 1; i < NEIGHBOR_MAX; i++)
        if (t->items[i].last_seen_ms < t->items[oldest].last_seen_ms) oldest = i;
    copy_from_msg(&t->items[oldest], m, now_ms);
}

void neighbor_expire(neighbor_table_t *t, uint32_t now_ms) {
    for (int i = 0; i < NEIGHBOR_MAX; i++)
        if (t->items[i].used && (now_ms - t->items[i].last_seen_ms) > NEIGHBOR_TTL_MS)
            t->items[i].used = false;
}
