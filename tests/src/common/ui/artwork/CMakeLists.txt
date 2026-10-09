find_package(JPEG REQUIRED)
find_package(PNG REQUIRED)
stroom_test(artwork-decode ui test_artwork_decode.c "${STROOM_ROOT}/src/common/ui/artwork/decode.c"
  "${STROOM_ROOT}/src/common/ui/artwork/image.c"
  "${STROOM_ROOT}/src/common/ui/artwork/jpeg.c"
  "${STROOM_ROOT}/src/common/ui/artwork/png.c")
target_link_libraries(test-artwork-decode PRIVATE JPEG::JPEG PNG::PNG)

if(STROOM_DIAGNOSTICS)
  stroom_test(artwork-view ui test_artwork_view.c
    "${STROOM_ROOT}/src/common/ui/artwork/view.c"
    "${STROOM_ROOT}/src/common/ui/shared/style.c")
  target_include_directories(test-artwork-view PRIVATE ../shared/stubs)
endif()

find_package(Threads REQUIRED)
stroom_test(artwork-worker ui test_artwork_worker.c
  "${STROOM_ROOT}/src/common/ui/artwork/worker.c"
  "${STROOM_ROOT}/src/common/platform/thread/worker.c"
  "${STROOM_ROOT}/src/common/platform/thread/scheduler.c")
target_include_directories(test-artwork-worker PRIVATE worker_stubs)
target_link_libraries(test-artwork-worker PRIVATE Threads::Threads)
target_compile_definitions(test-artwork-worker PRIVATE _DEFAULT_SOURCE _POSIX_C_SOURCE=200809L)
set_tests_properties(artwork-worker PROPERTIES TIMEOUT 10)
