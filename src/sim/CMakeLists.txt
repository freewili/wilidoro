cmake_minimum_required(VERSION 3.20)
project(wilidoro_sim C)
set(CMAKE_C_STANDARD 11)

add_compile_definitions(WILIDORO_SIM=1)
set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../config/lv_conf.h CACHE STRING "" FORCE)

# This is a separate CMake project from the root and does NOT inherit the
# root's WILIDORO_BOARD -- it needs its own option, same default (fw2).
set(WILIDORO_BOARD "fw2" CACHE STRING "Simulated board: fw2 or og")
if(WILIDORO_BOARD STREQUAL "og")
    add_compile_definitions(WILIDORO_BOARD_OG=1)
endif()

find_package(SDL2 REQUIRED)

set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/lvgl lvgl_build)

# config/lv_conf.h turns on LV_USE_SDL when WILIDORO_SIM is defined, so LVGL's
# own SDL driver sources (drivers/sdl/lv_sdl_window.c, lv_sdl_mouse.c, ...) are
# compiled as part of the `lvgl` target and #include <SDL2/SDL.h> themselves.
# The lvgl target therefore needs SDL2's include dir and to link SDL2, not just
# wilidoro_sim.
target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})

set(WILIDORO_SIM_SOURCES
    main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/pomodoro.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/beacon.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/beacon_rx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/tilt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/dimming.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../hal/hal_sim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../app/app_model.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../app/app.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../app/timer_view.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../app/led_pattern.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../app/sound.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui/ui.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui/screen_timer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui/screen_settings.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui/screen_nearby.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui/theme.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui/theme_neon.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui/theme_arcade.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui/theme_flip.c
)
# dvi_view.c is FW2-only -- the OG has no HSTX, exactly as in cmake/board_og.cmake.
if(NOT WILIDORO_BOARD STREQUAL "og")
    list(APPEND WILIDORO_SIM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../app/dvi_view.c)
endif()

add_executable(wilidoro_sim ${WILIDORO_SIM_SOURCES})
target_include_directories(wilidoro_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core
    ${CMAKE_CURRENT_SOURCE_DIR}/../hal
    ${CMAKE_CURRENT_SOURCE_DIR}/../app
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui
    ${CMAKE_CURRENT_SOURCE_DIR}/../../config
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(wilidoro_sim PRIVATE lvgl ${SDL2_LIBRARIES} m)
