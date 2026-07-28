#include "theme.h"
#if defined(WILIDORO_BOARD_OG)
/* Plan OG-C links the other two. Until then every index resolves to neon. */
const theme_t *theme_get(uint8_t idx) { (void)idx; return &THEME_NEON; }
#else
const theme_t *theme_get(uint8_t idx) {
    switch (idx) {
        case 1: return &THEME_ARCADE;
        case 2: return &THEME_FLIP;
        default: return &THEME_NEON;
    }
}
#endif
