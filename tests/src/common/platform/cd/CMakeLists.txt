stroom_test(cd-drive test_process test_drive.c "${STROOM_ROOT}/src/common/platform/cd/drive.c")
target_include_directories(test-cd-drive PRIVATE stubs "${STROOM_ROOT}/src/common")
target_compile_definitions(test-cd-drive PRIVATE _DEFAULT_SOURCE)
