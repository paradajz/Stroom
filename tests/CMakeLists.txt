cmake_minimum_required(VERSION 3.21)
project(stroom_tests LANGUAGES C)

option(STROOM_DIAGNOSTICS "Exercise diagnostic recording and requests" ON)

include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/project-options.cmake")

enable_testing()

# Unity is a pinned dependency of the host tests only.
add_subdirectory("${STROOM_ROOT}/third_party/Unity" libraries/unity EXCLUDE_FROM_ALL)
include("${STROOM_ROOT}/src/common/audio/portable.cmake")
add_subdirectory("${STROOM_ROOT}/src/common/milkdrop" libraries/milkdrop)
include("${STROOM_ROOT}/src/common/ui/portable.cmake")

# Common registration keeps assertion checks and working directories consistent.
function(stroom_test name library)
  add_executable(test-${name} ${ARGN})
  target_link_libraries(test-${name} PRIVATE ${library} unity::framework)
  add_test(NAME ${name} COMMAND test-${name})
  set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY "${PROJECT_BINARY_DIR}")
endfunction()

add_subdirectory(src/common)
add_subdirectory(src/stroom)

add_subdirectory(src/benchmark)
