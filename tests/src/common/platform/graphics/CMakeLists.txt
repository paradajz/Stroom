stroom_test(display-pacing unity::framework test_display.c "${STROOM_ROOT}/src/common/platform/graphics/display.c")
target_include_directories(test-display-pacing PRIVATE stubs "${STROOM_ROOT}/src/common")

stroom_test(display-readback unity::framework test_readback.c "${STROOM_ROOT}/src/common/platform/graphics/readback.c")
target_include_directories(test-display-readback PRIVATE stubs "${STROOM_ROOT}/src/common")
