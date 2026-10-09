include("${CMAKE_CURRENT_LIST_DIR}/portable.cmake")
add_executable(benchmark main.c color_curve.c ../common/profiling/benchmark.c)
target_link_libraries(benchmark PRIVATE preset_benchmark)
