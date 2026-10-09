stroom_test(cd-drive test_process test_drive.c "${STROOM_ROOT}/src/common/platform/cd/drive.c")
target_include_directories(test-cd-drive PRIVATE stubs "${STROOM_ROOT}/src/common")
target_compile_definitions(test-cd-drive PRIVATE _DEFAULT_SOURCE)

find_package(Threads REQUIRED)
stroom_test(cd-reader unity::framework test_reader.c
  "${STROOM_ROOT}/src/common/platform/cd/reader.c"
  "${STROOM_ROOT}/src/common/platform/cd/drive.c")
target_link_libraries(test-cd-reader PRIVATE Threads::Threads)
target_include_directories(test-cd-reader PRIVATE stubs "${STROOM_ROOT}/src/common")
target_compile_definitions(test-cd-reader PRIVATE _DEFAULT_SOURCE)
set_tests_properties(cd-reader PROPERTIES TIMEOUT 15)
