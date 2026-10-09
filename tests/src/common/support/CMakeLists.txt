add_library(test_process STATIC process.c)
target_compile_definitions(test_process PRIVATE _POSIX_C_SOURCE=200809L)
target_include_directories(test_process PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(test_process PUBLIC unity::framework)

# Scripts generate fixtures and run JS assertions; CMake compiles and links the C tests.
function(stroom_generated_test name library script)
  cmake_parse_arguments(fixture "" "" "ARGS;DEPENDS" ${ARGN})
  set(source "${CMAKE_CURRENT_BINARY_DIR}/generated/${name}.c")
  add_custom_target(generate-test-${name}
    BYPRODUCTS "${source}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "${NODE_EXECUTABLE}" "${script}" "${source}" ${fixture_ARGS}
    DEPENDS "${script}" "${STROOM_ROOT}/tests/src/common/support/unity.mjs" ${fixture_DEPENDS}
    VERBATIM
  )
  stroom_test(${name} ${library} "${source}")
  add_dependencies(test-${name} generate-test-${name})
endfunction()

add_library(test_worker_driver STATIC worker_driver.c)
target_include_directories(test_worker_driver PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/.." "${STROOM_ROOT}/src/common")
target_link_libraries(test_worker_driver PUBLIC unity::framework)
target_link_options(test_worker_driver INTERFACE -Wl,--wrap=platform_worker_open)
