/* src/ui/theme_flip.c — placeholder until Task 4 */
#include "theme.h"
static void f_build(lv_obj_t *f){ THEME_NEON.build(f); }
static void f_update(const timer_view_t *v){ THEME_NEON.update(v); }
const theme_t THEME_FLIP = { .name="Flip Clock", .bg=0x221f1d, .accent=0xC8503C, .cool=0x8a7d6b, .build=f_build, .update=f_update };
