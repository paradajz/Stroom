stroom_test(ariacast audio test_ariacast.c
  "${STROOM_ROOT}/src/common/platform/network/socket_error.c"
  "${STROOM_ROOT}/src/common/audio/network/ariacast/server.c"
  "${STROOM_ROOT}/src/common/audio/network/ariacast/client.c"
  "${STROOM_ROOT}/src/common/audio/network/ariacast/socket.c"
  "${STROOM_ROOT}/src/common/audio/network/ariacast/discovery.c"
  "${STROOM_ROOT}/src/common/audio/network/ariacast/statistics.c")
target_include_directories(test-ariacast PRIVATE stubs)
target_compile_definitions(test-ariacast PRIVATE _DEFAULT_SOURCE)
set_tests_properties(ariacast PROPERTIES RUN_SERIAL TRUE)

if(STROOM_DIAGNOSTICS)
  target_sources(test-ariacast PRIVATE
    "${STROOM_ROOT}/src/common/audio/network/ariacast/diagnostics/poll.c"
    "${STROOM_ROOT}/src/common/audio/network/ariacast/diagnostics/requests.c")
  stroom_test(aria-diagnostic_capture audio test_diagnostic_capture.c)
endif()
