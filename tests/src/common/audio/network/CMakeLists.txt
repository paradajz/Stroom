stroom_test(network audio test_network.c "${STROOM_ROOT}/src/common/audio/network/network.c" "${STROOM_ROOT}/src/common/platform/network/runtime.c" "${STROOM_ROOT}/src/common/platform/iop/modules.c" "${STROOM_ROOT}/src/common/platform/thread/worker.c" "${STROOM_ROOT}/src/common/platform/network/socket_mode.c")
target_include_directories(test-network PRIVATE stubs "${PROJECT_SOURCE_DIR}/src/common/stubs" "${PROJECT_SOURCE_DIR}/src/common/stubs/iop" "${STROOM_ROOT}/src/common")
target_compile_definitions(test-network PRIVATE _DEFAULT_SOURCE)

target_link_libraries(test-network PRIVATE test_worker_driver)

add_subdirectory(ariacast)

stroom_test(network-startup audio test_network_startup.c
  "${STROOM_ROOT}/src/common/audio/network/network.c")

stroom_test(cd-stream audio test_cd_stream.c "${STROOM_ROOT}/src/common/audio/network/cd_stream/server.c" "${STROOM_ROOT}/src/common/platform/network/socket_error.c" "${STROOM_ROOT}/src/common/platform/network/tcp_state.c")
target_compile_definitions(test-cd-stream PRIVATE _DEFAULT_SOURCE)
target_link_options(test-cd-stream PRIVATE -Wl,--wrap=send -Wl,--wrap=recvfrom -Wl,--wrap=accept -Wl,--wrap=setsockopt -Wl,--wrap=getsockopt)
set_tests_properties(cd-stream PROPERTIES RUN_SERIAL TRUE TIMEOUT 20)

add_executable(cd-stream-fixture cd_stream_fixture.c "${STROOM_ROOT}/src/common/audio/network/cd_stream/server.c" "${STROOM_ROOT}/src/common/platform/network/socket_error.c" "${STROOM_ROOT}/src/common/platform/network/tcp_state.c")
target_link_libraries(cd-stream-fixture PRIVATE audio)
target_compile_definitions(cd-stream-fixture PRIVATE _DEFAULT_SOURCE)
target_link_options(cd-stream-fixture PRIVATE -Wl,--wrap=bind)

stroom_test(cd-stream-worker audio test_cd_stream_worker.c "${STROOM_ROOT}/src/common/audio/network/cd_stream/stream.c")
