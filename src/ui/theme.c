#include "theme.h"
const theme_t *theme_get(uint8_t idx) {
    switch (idx) {
        case 1: return &THEME_ARCADE;
        case 2: return &THEME_FLIP;
        default: return &THEME_NEON;
    }
}
