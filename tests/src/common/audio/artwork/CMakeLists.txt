stroom_test(artwork-http audio test_artwork_http.c
  "${STROOM_ROOT}/src/common/platform/network/socket_error.c" "${STROOM_ROOT}/src/common/audio/artwork/receiver.c")
target_include_directories(test-artwork-http PRIVATE ../network/ariacast/stubs)
target_compile_definitions(test-artwork-http PRIVATE _DEFAULT_SOURCE)
