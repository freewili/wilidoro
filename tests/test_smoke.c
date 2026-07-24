#include "greatest.h"

TEST harness_works(void) {
    ASSERT_EQ(2, 1 + 1);
    PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();
    RUN_TEST(harness_works);
    GREATEST_MAIN_END();
}
