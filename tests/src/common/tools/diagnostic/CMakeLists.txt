stroom_test(diagnostic-download test_process test_diagnostic.c)
target_compile_definitions(test-diagnostic-download PRIVATE
  NODE_PATH="${NODE_EXECUTABLE}"
  FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/diagnostic_fixture.mjs"
  CLIENT_PATH="${STROOM_ROOT}/tools/diagnostic/capture.mjs")
set_tests_properties(diagnostic-download PROPERTIES RUN_SERIAL TRUE)
