cmake_minimum_required(VERSION 3.20)
project(wilidoro_tests C)
set(CMAKE_C_STANDARD 11)
enable_testing()

set(CORE ${CMAKE_CURRENT_SOURCE_DIR}/../src/core)

function(add_core_test name)
  add_executable(${name} ${name}.c ${ARGN})
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CORE})
  target_compile_options(${name} PRIVATE -Wall -Wextra)
  target_link_libraries(${name} m)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_core_test(test_smoke)
add_core_test(test_pomodoro ${CORE}/pomodoro.c)
add_core_test(test_beacon ${CORE}/beacon.c)
add_core_test(test_beacon_rx ${CORE}/beacon_rx.c ${CORE}/beacon.c)
add_core_test(test_tilt ${CORE}/tilt.c)
add_core_test(test_dimming ${CORE}/dimming.c)

add_executable(test_app_model test_app_model.c ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/app_model.c ${CORE}/pomodoro.c)
target_include_directories(test_app_model PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CORE} ${CMAKE_CURRENT_SOURCE_DIR}/../src/app)
target_compile_options(test_app_model PRIVATE -Wall -Wextra)
target_link_libraries(test_app_model m)
add_test(NAME test_app_model COMMAND test_app_model)

add_executable(test_timer_view test_timer_view.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/timer_view.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/pomodoro.c)
target_include_directories(test_timer_view PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CORE} ${CMAKE_CURRENT_SOURCE_DIR}/../src/app)
target_compile_options(test_timer_view PRIVATE -Wall -Wextra)
target_link_libraries(test_timer_view m)
add_test(NAME test_timer_view COMMAND test_timer_view)

add_executable(test_led_pattern test_led_pattern.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/led_pattern.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/timer_view.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/pomodoro.c)
target_include_directories(test_led_pattern PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CORE} ${CMAKE_CURRENT_SOURCE_DIR}/../src/app)
target_compile_options(test_led_pattern PRIVATE -Wall -Wextra)
target_link_libraries(test_led_pattern m)
add_test(NAME test_led_pattern COMMAND test_led_pattern)

add_executable(test_sound test_sound.c ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/sound.c)
target_include_directories(test_sound PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../src/app)
target_compile_options(test_sound PRIVATE -Wall -Wextra)
target_link_libraries(test_sound m)
add_test(NAME test_sound COMMAND test_sound)

add_executable(test_dvi_view test_dvi_view.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/dvi_view.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/timer_view.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/pomodoro.c)
target_include_directories(test_dvi_view PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CORE} ${CMAKE_CURRENT_SOURCE_DIR}/../src/app)
target_compile_options(test_dvi_view PRIVATE -Wall -Wextra)
target_link_libraries(test_dvi_view m)
add_test(NAME test_dvi_view COMMAND test_dvi_view)
