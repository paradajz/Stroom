get_filename_component(STROOM_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Single-configuration builds default to Release unless explicitly configured.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Keep command-line tools and the console ELF at the documented build paths.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_compile_options(-Wall -Wextra -Werror)

set(PRESET_ROOT "${STROOM_ROOT}/third_party/presets-milkdrop-original"
    CACHE PATH "MilkDrop preset library")

# Migrate the previous bundled location without changing custom directories.
if(MILKDROP_BENCHMARK_DIR STREQUAL "${STROOM_ROOT}/milkdrop/benchmark" OR
   MILKDROP_BENCHMARK_DIR STREQUAL "${STROOM_ROOT}/src/milkdrop/benchmarks")
  unset(MILKDROP_BENCHMARK_DIR CACHE)
endif()
set(MILKDROP_BENCHMARK_DIR "${STROOM_ROOT}/src/common/milkdrop/benchmarks"
    CACHE PATH "Directory containing full MilkDrop benchmark reports")
file(GLOB MILKDROP_BENCHMARK_INPUTS CONFIGURE_DEPENDS "${MILKDROP_BENCHMARK_DIR}/*.json")

find_program(NODE_EXECUTABLE NAMES node nodejs)

if(NOT NODE_EXECUTABLE)
  file(GLOB _vscode_node "$ENV{HOME}/.vscode-server/bin/*/node")

  if(_vscode_node)
    list(GET _vscode_node 0 NODE_EXECUTABLE)
    set(NODE_EXECUTABLE "${NODE_EXECUTABLE}"
        CACHE FILEPATH "Host Node.js executable" FORCE)
  else()
    message(FATAL_ERROR
      "Node.js 18+ required for preset compilation; set NODE_EXECUTABLE")
  endif()
endif()

set(MILKDROP_PRESET_SELECTION "playback" CACHE STRING "Preset selection: playback, all-compatible or quick")
set_property(CACHE MILKDROP_PRESET_SELECTION PROPERTY STRINGS playback all-compatible quick)
if(NOT MILKDROP_PRESET_SELECTION STREQUAL "playback" AND NOT MILKDROP_PRESET_SELECTION STREQUAL "all-compatible" AND NOT MILKDROP_PRESET_SELECTION STREQUAL "quick")
  message(FATAL_ERROR "Unknown MILKDROP_PRESET_SELECTION: ${MILKDROP_PRESET_SELECTION}")
endif()

set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")

option(STROOM_DIAGNOSTICS "Enable console logs and private network diagnostics" OFF)
add_compile_definitions(STROOM_DIAGNOSTICS=$<BOOL:${STROOM_DIAGNOSTICS}>)

set(STROOM_APP "stroom" CACHE STRING "Application to build: stroom or benchmark")
set_property(CACHE STROOM_APP PROPERTY STRINGS stroom benchmark)
if(NOT STROOM_APP STREQUAL "stroom" AND NOT STROOM_APP STREQUAL "benchmark")
  message(FATAL_ERROR "Unknown STROOM_APP: ${STROOM_APP}")
endif()
set(STROOM_PRESET_BENCHMARK OFF)
if(STROOM_APP STREQUAL "benchmark")
  set(STROOM_PRESET_BENCHMARK ON)
  if(NOT STROOM_DIAGNOSTICS)
    message(FATAL_ERROR "The benchmark app requires STROOM_DIAGNOSTICS")
  endif()
endif()
option(STROOM_BENCHMARK_PROFILE "Measure detailed per-stage benchmark timings" ON)

include("${CMAKE_CURRENT_LIST_DIR}/contracts.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/milkdrop-compiler.cmake")
