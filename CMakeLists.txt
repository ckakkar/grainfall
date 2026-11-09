cmake_minimum_required(VERSION 3.24)
project(grainfall LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(GRAINFALL_BUILD_APP "Build the SDL3 desktop app" ON)
option(GRAINFALL_BUILD_TESTS "Build the test runner" ON)

# ---------------------------------------------------------------------------
# Engine: everything that runs without a window (core + sim + io + scenes).
# ---------------------------------------------------------------------------
add_library(grainfall_engine STATIC
  src/core/material.cpp
  src/core/world.cpp
  src/sim/simulation.cpp
  src/io/save.cpp
  src/scenes/scenes.cpp
)
target_include_directories(grainfall_engine PUBLIC src)
target_compile_options(grainfall_engine PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# Tests: a tiny self-registering framework in tests/testing.hpp.
# ---------------------------------------------------------------------------
if(GRAINFALL_BUILD_TESTS)
  enable_testing()
  add_executable(grainfall_tests
    tests/test_main.cpp
    tests/test_core.cpp
    tests/test_sim.cpp
    tests/test_io.cpp
    tests/test_scenes.cpp
  )
  target_link_libraries(grainfall_tests PRIVATE grainfall_engine)
  target_include_directories(grainfall_tests PRIVATE tests)
  target_compile_options(grainfall_tests PRIVATE -Wall -Wextra)
  add_test(NAME grainfall_tests COMMAND grainfall_tests)
endif()

# ---------------------------------------------------------------------------
# App: SDL3 window, renderer, input, HUD. Also has a headless --bench mode.
# ---------------------------------------------------------------------------
if(GRAINFALL_BUILD_APP)
  find_package(SDL3 REQUIRED CONFIG)
  add_executable(grainfall
    src/app/main.cpp
    src/app/app.cpp
    src/app/renderer.cpp
  )
  target_link_libraries(grainfall PRIVATE grainfall_engine SDL3::SDL3)
  target_compile_options(grainfall PRIVATE -Wall -Wextra)
endif()
