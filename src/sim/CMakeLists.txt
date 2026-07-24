cmake_minimum_required(VERSION 3.20)
project(wilidoro_sim C)
set(CMAKE_C_STANDARD 11)

add_compile_definitions(WILIDORO_SIM=1)
set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../config/lv_conf.h CACHE STRING "" FORCE)

find_package(SDL2 REQUIRED)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/lvgl lvgl_build)

# config/lv_conf.h turns on LV_USE_SDL when WILIDORO_SIM is defined, so LVGL's
# own SDL driver sources (drivers/sdl/lv_sdl_window.c, lv_sdl_mouse.c, ...) are
# compiled as part of the `lvgl` target and #include <SDL2/SDL.h> themselves.
# The lvgl target therefore needs SDL2's include dir and to link SDL2, not just
# wilidoro_sim.
target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})

add_executable(wilidoro_sim
    main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/pomodoro.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/beacon.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/gestures.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/dimming.c
)
target_include_directories(wilidoro_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core
    ${CMAKE_CURRENT_SOURCE_DIR}/../hal
    ${CMAKE_CURRENT_SOURCE_DIR}/../../config
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(wilidoro_sim PRIVATE lvgl ${SDL2_LIBRARIES} m)
