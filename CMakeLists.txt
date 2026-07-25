cmake_minimum_required(VERSION 3.20)
set(PICO_BOARD freewili2 CACHE STRING "Board type")
list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/boards")
list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/wilibsp/bsp/boards")
include(pico_sdk_import.cmake)
project(wilidoro C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

# LVGL
set(LV_CONF_PATH ${CMAKE_CURRENT_LIST_DIR}/config/lv_conf.h CACHE STRING "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/lvgl)

# wilibsp BSP static library
add_subdirectory(wilibsp/bsp)

add_executable(wilidoro
    src/target/main.c
    src/target/lvgl_port.c
    src/target/bl_pwm.c
    src/core/pomodoro.c
    src/core/beacon.c
    src/core/gestures.c
    src/core/dimming.c
    src/hal/hal_target.c
    src/app/app_model.c
    src/app/app.c
    src/app/timer_view.c
    src/app/led_pattern.c
    src/ui/ui.c
    src/ui/screen_timer.c
    src/ui/screen_settings.c
    src/ui/screen_nearby.c
    src/ui/theme.c
    src/ui/theme_neon.c
    src/ui/theme_arcade.c
    src/ui/theme_flip.c
)
target_include_directories(wilidoro PRIVATE
    src/core src/hal src/target
    src/app src/ui
    wilibsp/bsp
    config
)
target_link_libraries(wilidoro PRIVATE
    pico_stdlib hardware_clocks hardware_gpio hardware_spi hardware_dma
    hardware_irq hardware_pwm hardware_i2c hardware_pio
    freewili2_bsp lvgl
)
pico_enable_stdio_usb(wilidoro 0)
pico_enable_stdio_uart(wilidoro 0)
pico_set_binary_type(wilidoro default)      # XIP from flash (LVGL too big for copy_to_ram)
pico_add_extra_outputs(wilidoro)
