include("${STROOM_ROOT}/src/benchmark/portable.cmake")

stroom_test(preset-benchmark preset_benchmark test_benchmark.c)
stroom_test(benchmark-runtime preset_benchmark test_runtime.c)
add_test(NAME benchmark-collector COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_collector.mjs")
add_test(NAME benchmark-audio COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_audio.mjs")
add_test(NAME benchmark-reports COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_reports.mjs")
add_test(NAME benchmark-quick-selection COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_quick_selection.mjs")
