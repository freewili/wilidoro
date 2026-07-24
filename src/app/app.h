#ifndef WILIDORO_APP_H
#define WILIDORO_APP_H
#include "pomodoro.h"
#include "app_model.h"

typedef enum { SCREEN_TIMER, SCREEN_SETTINGS, SCREEN_NEARBY } app_screen_t;

typedef struct {
    pomodoro_t       pomo;
    app_settings_t   settings;
    neighbor_table_t neighbors;
    app_screen_t     screen;
    bool             alarm_active;   /* focus ended, waiting for dismiss */
} app_t;

app_t *app(void);                    /* the single shared instance */
void   app_init(void);               /* build screens + start tick timer + load timer screen */
void   app_goto(app_screen_t s);     /* switch screens with a slide anim */
#endif
