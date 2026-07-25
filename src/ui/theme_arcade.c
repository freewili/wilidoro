/* src/ui/theme_arcade.c — placeholder until Task 3 */
#include "theme.h"
static void a_build(lv_obj_t *f){ THEME_NEON.build(f); }
static void a_update(const timer_view_t *v){ THEME_NEON.update(v); }
const theme_t THEME_ARCADE = { .name="Arcade", .bg=0x2d1b4e, .accent=0xff4757, .cool=0x2ed573, .build=a_build, .update=a_update };
