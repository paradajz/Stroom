add_test(NAME setup-profiles COMMAND "${NODE_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/test_profiles.mjs" "${STROOM_ROOT}")
add_test(NAME setup-installer COMMAND "${NODE_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/test_installer.mjs" "${STROOM_ROOT}")
