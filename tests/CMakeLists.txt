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
