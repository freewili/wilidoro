# wilibsp BSP static library
add_subdirectory(wilibsp/bsp)
# Size the HSTX DVI framebuffer for wilidoro's 480x240 focus display. PUBLIC so the
# app and the BSP agree. The buffer is 200 + H*(11 + W/2) + (480-H)*8 dwords:
# 480x240 = 244 KB, which fits alongside our ~187 KB of BSS. The BSP default
# 480x320 is 320 KB and would leave ~5 KB for heap.
target_compile_definitions(freewili2_bsp PUBLIC HSTX_VID_W_MAX=480 HSTX_VID_H_MAX=240)

add_executable(wilidoro
    src/target/main.c
    src/target/lvgl_port.c
    src/target/bl_pwm.c
    src/core/pomodoro.c
    src/core/beacon.c
    src/core/beacon_rx.c
    src/core/tilt.c
    src/core/dimming.c
    src/hal/hal_target.c
    src/app/app_model.c
    src/app/app.c
    src/app/timer_view.c
    src/app/led_pattern.c
    src/app/sound.c
    src/app/dvi_view.c
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

# A DISPLAY app is loaded from /apps/ and must never contain QSPI-flash
# blocks. The BSP helper emits the validated PSRAM UF2 plus the required app
# metadata record (also used by the PAGE-hold About screen).
fw2_psram_app(wilidoro
    NAME "Wilidoro"
    VERSION 001
    DESCRIPTION "Pomodoro timer with themed countdown, LEDs, chimes and tilt pause"
    REPOSITORY "https://github.com/freewili/wilidoro")

# The generic BSP check proves the UF2 only targets PSRAM. Keep the app's
# SRAM bootstrap contract checked too: static storage must not consume the
# fixed initial stack at 0x20070000.
add_custom_command(TARGET wilidoro POST_BUILD
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_psram_layout.py
            $<TARGET_FILE:wilidoro> ${CMAKE_OBJDUMP}
            $<TARGET_FILE_DIR:wilidoro>/wilidoro.uf2
    VERBATIM)
