cmake_minimum_required(VERSION 3.20)

# Which board this configure builds. PICO_BOARD is a single global cache value
# and the two boards are different silicon, so one configure cannot build both.
set(WILIDORO_BOARD "fw2" CACHE STRING "Target board: fw2 (FreeWili 2) or og (FreeWili OG)")
set_property(CACHE WILIDORO_BOARD PROPERTY STRINGS fw2 og)

if(WILIDORO_BOARD STREQUAL "og")
    # Never pass -DPICO_BOARD on a command line; it silently reverts the config.
    set(PICO_BOARD freewili_og CACHE STRING "Board type" FORCE)
    list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/wiliOGbsp/bsp/boards")
    # UART0 is the inter-CPU link on BOTH CPUs and must never carry stdio.
    set(PICO_STDIO_UART 0 CACHE BOOL "stdio over UART (forced off: UART0 is the link)" FORCE)
elseif(WILIDORO_BOARD STREQUAL "fw2")
    set(PICO_BOARD freewili2 CACHE STRING "Board type")
    set(PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/wilibsp/bsp/boards")
    # The current BSP includes PIO-USB support; its SDK target must be located
    # before pico_sdk_init() creates the dependency graph.
    set(PICO_PIO_USB_PATH "${CMAKE_CURRENT_LIST_DIR}/wilibsp/bsp/third_party/Pico-PIO-USB")
    option(FW2_AGENTIO "Build the FW2 remote-control/capture harness" ON)
else()
    message(FATAL_ERROR "WILIDORO_BOARD must be 'fw2' or 'og', got '${WILIDORO_BOARD}'")
endif()

include(pico_sdk_import.cmake)
project(wilidoro C CXX ASM)
set(CMAKE_C_STANDARD 11)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
pico_sdk_init()

# LVGL — shared by both boards; lv_conf.h branches on board where it must.
set(LV_CONF_PATH ${CMAKE_CURRENT_LIST_DIR}/config/lv_conf.h CACHE STRING "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/lvgl)

if(WILIDORO_BOARD STREQUAL "og")
    include(cmake/board_og.cmake)
else()
    include(cmake/board_fw2.cmake)
endif()
