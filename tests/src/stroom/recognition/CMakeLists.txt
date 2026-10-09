stroom_test(cd-lookup-probe audio test_client.c "${STROOM_ROOT}/src/common/platform/thread/worker.c"
  "${STROOM_ROOT}/src/common/platform/network/socket_mode.c"
  "${STROOM_ROOT}/src/stroom/recognition/client.c")
target_include_directories(test-cd-lookup-probe PRIVATE "${STROOM_ROOT}/src/stroom")
target_link_libraries(test-cd-lookup-probe PRIVATE test_worker_driver)
target_include_directories(test-cd-lookup-probe PRIVATE
  "${STROOM_ROOT}/tests/src/common/platform/thread/worker_stubs"
  stubs)
target_compile_definitions(test-cd-lookup-probe PRIVATE _DEFAULT_SOURCE)

# Intercept host I/O while compiling the client as a separate translation unit.
target_link_options(test-cd-lookup-probe PRIVATE
  -Wl,--wrap=socket -Wl,--wrap=sendto -Wl,--wrap=recvfrom
  -Wl,--wrap=close -Wl,--wrap=usleep)
