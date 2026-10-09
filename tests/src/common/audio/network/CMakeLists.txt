stroom_test(network audio test_network.c "${STROOM_ROOT}/src/common/audio/network/network.c" "${STROOM_ROOT}/src/common/platform/network/runtime.c" "${STROOM_ROOT}/src/common/platform/iop/modules.c" "${STROOM_ROOT}/src/common/platform/thread/worker.c" "${STROOM_ROOT}/src/common/platform/network/socket_mode.c")
target_include_directories(test-network PRIVATE stubs "${PROJECT_SOURCE_DIR}/src/common/stubs" "${PROJECT_SOURCE_DIR}/src/common/stubs/iop" "${STROOM_ROOT}/src/common")
target_compile_definitions(test-network PRIVATE _DEFAULT_SOURCE)

target_link_libraries(test-network PRIVATE test_worker_driver)

add_subdirectory(ariacast)

stroom_test(network-startup audio test_network_startup.c
  "${STROOM_ROOT}/src/common/audio/network/network.c")
