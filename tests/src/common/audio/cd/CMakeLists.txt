foreach(test IN ITEMS cd_controller cd_format cd_pcm cd_scan cd_playback)
  stroom_test(${test} audio "test_${test}.c")
endforeach()

stroom_test(cd-output audio test_cd_output.c "${STROOM_ROOT}/src/common/audio/cd/cd_output.c")
target_include_directories(test-cd-output PRIVATE "${STROOM_ROOT}/tests/src/common/platform/audio/stubs" "${STROOM_ROOT}/src/common")
target_compile_definitions(test-cd-output PRIVATE _DEFAULT_SOURCE)

target_link_options(test-cd-output PRIVATE -Wl,--wrap=cd_resample)

target_sources(test-cd-output PRIVATE
  "${STROOM_ROOT}/src/common/audio/output/device.c"
  "${STROOM_ROOT}/src/common/platform/audio/output.c")

stroom_test(cd-worker audio test_cd_worker.c "${STROOM_ROOT}/src/common/platform/thread/worker.c"
  "${STROOM_ROOT}/src/common/audio/cd/cd.c")
target_link_libraries(test-cd-worker PRIVATE test_worker_driver)
target_include_directories(test-cd-worker PRIVATE "${STROOM_ROOT}/tests/src/common/platform/thread/worker_stubs")
target_compile_definitions(test-cd-worker PRIVATE _DEFAULT_SOURCE)
target_link_options(test-cd-worker PRIVATE -Wl,--wrap=usleep)

stroom_test(cd-lookup-toc audio test_cd_lookup_toc.c)
