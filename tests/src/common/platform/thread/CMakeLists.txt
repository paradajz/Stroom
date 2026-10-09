stroom_test(ps2-scheduler unity::framework test_scheduler.c "${STROOM_ROOT}/src/common/platform/thread/scheduler.c")
target_include_directories(test-ps2-scheduler PRIVATE "${PROJECT_SOURCE_DIR}/src/common/stubs/scheduler" "${STROOM_ROOT}/src/common")

stroom_test(ps2-worker unity::framework test_worker.c "${STROOM_ROOT}/src/common/platform/thread/worker.c")
target_include_directories(test-ps2-worker PRIVATE worker_stubs "${STROOM_ROOT}/src/common")
target_compile_definitions(test-ps2-worker PRIVATE _DEFAULT_SOURCE)

target_link_options(test-ps2-worker PRIVATE "-Wl,--wrap=printf")
