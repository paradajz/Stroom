add_test(NAME sdk-ports-selection
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_ports.mjs" "${STROOM_ROOT}")

add_test(NAME sdk-clean
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_clean.mjs" "${STROOM_ROOT}")

add_test(NAME sdk-dependencies COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_dependencies.mjs")
