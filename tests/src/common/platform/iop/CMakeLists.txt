stroom_test(ps2-iop unity::framework test_modules.c "${STROOM_ROOT}/src/common/platform/iop/modules.c")
target_include_directories(test-ps2-iop PRIVATE "${PROJECT_SOURCE_DIR}/src/common/stubs/iop" "${STROOM_ROOT}/src/common")
