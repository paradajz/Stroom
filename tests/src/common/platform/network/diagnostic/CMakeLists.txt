stroom_test(network-diagnostic unity::framework test_iop.c "${STROOM_ROOT}/src/common/platform/network/diagnostic/iop.c")
target_include_directories(test-network-diagnostic PRIVATE "${PROJECT_SOURCE_DIR}/src/common/stubs/network_diagnostic" "${STROOM_ROOT}/src/common")
