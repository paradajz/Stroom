stroom_test(socket-error unity::framework test_socket_error.c "${STROOM_ROOT}/src/common/platform/network/socket_error.c")
target_include_directories(test-socket-error PRIVATE "${STROOM_ROOT}/src/common")

add_subdirectory(rpc)
add_subdirectory(diagnostic)

stroom_test(network-runtime test_process test_runtime.c "${STROOM_ROOT}/src/common/platform/network/runtime.c")
target_include_directories(test-network-runtime PRIVATE runtime_stubs "${STROOM_ROOT}/src/common")
target_compile_definitions(test-network-runtime PRIVATE _DEFAULT_SOURCE)

stroom_test(socket-mode unity::framework test_socket_mode.c "${STROOM_ROOT}/src/common/platform/network/socket_mode.c")
target_include_directories(test-socket-mode PRIVATE socket_stubs "${STROOM_ROOT}/src/common")
