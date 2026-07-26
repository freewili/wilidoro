#ifndef WILIDORO_APP_H
#define WILIDORO_APP_H
#include "pomodoro.h"
#include "app_model.h"
#include "sound.h"

typedef enum { SCREEN_TIMER, SCREEN_SETTINGS, SCREEN_NEARBY } app_screen_t;

typedef struct {
    pomodoro_t       pomo;
    app_settings_t   settings;
    neighbor_table_t neighbors;
    app_screen_t     screen;
    bool             alarm_active;   /* focus ended, waiting for dismiss */
    sound_player_t   sound;
} app_t;

app_t *app(void);                    /* the single shared instance */
void   app_init(void);               /* build screens + start tick timer + load timer screen */
void   app_goto(app_screen_t s);     /* switch screens with a slide anim */

/* Play `id` in the current theme at the current volume, pre-empting anything
   already sounding. app_sound_stop() silences immediately and idles the codec. */
void   app_sound(sound_id_t id);
void   app_sound_stop(void);
void   app_dvi_apply(void);          /* re-apply settings.dvi_on to the hardware */
#endif
