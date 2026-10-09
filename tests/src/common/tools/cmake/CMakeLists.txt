add_test(NAME cmake-preset-selection
  COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_presets.mjs"
    "${STROOM_ROOT}" "${CMAKE_COMMAND}")
