# FreeWili OG: two RP2040s, so two executables. The display app is declared
# FIRST -- fwog_embed_display_image() resolves FWOG_DISPLAY_FIRMWARE to a
# target at configure time, so a display app declared after the main app
# cannot be selected as the payload.

# genimage.py runs at build time to embed the display firmware.
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Read by the BSP's fwog_add_uf2_info() / fwog_embed_display_image().
execute_process(
    COMMAND git describe --always --dirty
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
    OUTPUT_VARIABLE FWOG_GIT_DESCRIBE OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0 OR FWOG_GIT_DESCRIBE STREQUAL "")
    set(FWOG_GIT_DESCRIBE "dev")
endif()
execute_process(
    COMMAND git log -1 --format=%ct
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
    OUTPUT_VARIABLE FWOG_GIT_COMMIT_TS OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET RESULT_VARIABLE _ts_rc)
if(NOT _ts_rc EQUAL 0 OR NOT FWOG_GIT_COMMIT_TS MATCHES "^[0-9]+$")
    set(FWOG_GIT_COMMIT_TS 0)
endif()

add_subdirectory(wiliOGbsp/bsp)

# ---- display CPU ----
add_executable(wilidoro_display
    src/target_og/main.c
)
target_include_directories(wilidoro_display PRIVATE
    src/core src/hal src/app src/ui src/target_og
    config
)
target_link_libraries(wilidoro_display PRIVATE
    pico_stdlib hardware_clocks hardware_gpio hardware_spi hardware_dma
    hardware_irq hardware_pwm hardware_i2c hardware_pio
    fwog_display_bsp
)
fwog_display_app(wilidoro_display
    VERSION 001
    DESCRIPTION "Pomodoro timer: themed countdown, LED progress ring, chimes and tilt-to-pause")

# ---- main CPU ----
# In Plan A this app exists to carry the display image onto the board: a
# display app cannot be UF2-flashed directly. Plan OG-D gives it the radio.
set(FWOG_DISPLAY_FIRMWARE "wilidoro_display" CACHE STRING "Display image to embed" FORCE)

add_executable(wilidoro_main
    src/main_og/main.c
)
target_include_directories(wilidoro_main PRIVATE
    src/core src/hal src/app src/main_og
)
target_link_libraries(wilidoro_main PRIVATE
    pico_stdlib hardware_clocks hardware_gpio hardware_spi hardware_pio
    fwog_main_bsp
)
# Generate and compile in the display image blob. fwog_main_app() does NOT do
# this (the BSP keeps the two separable), and fwog_display_update_run() -- the
# only supported way a display app reaches the display CPU -- references
# fwog_display_image/_end/_info, so without this the main app does not link.
# Must come AFTER target_link_libraries: the generated display_image.c
# includes "display_update/display_image.h" off fwog_main_bsp's include path.
fwog_embed_display_image(wilidoro_main)
fwog_main_app(wilidoro_main
    VERSION 001
    DESCRIPTION "Pomodoro timer, main CPU: carries the display image and (from Plan OG-D) the sub-GHz beacon")
