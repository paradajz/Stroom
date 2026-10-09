stroom_test(ps2-sleep unity::framework test_sleep.c "${STROOM_ROOT}/src/common/platform/time/sleep.c")
target_include_directories(test-ps2-sleep PRIVATE stubs "${STROOM_ROOT}/src/common")

stroom_test(ps2-clock unity::framework test_clock.c "${STROOM_ROOT}/src/common/platform/time/clock.c")
target_include_directories(test-ps2-clock PRIVATE stubs "${STROOM_ROOT}/src/common")
