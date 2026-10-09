add_library(preset_benchmark STATIC
  "${STROOM_ROOT}/src/benchmark/benchmark.c"
  "${STROOM_ROOT}/src/benchmark/runtime.c"
)
target_include_directories(preset_benchmark PUBLIC "${STROOM_ROOT}/src")
target_link_libraries(preset_benchmark PUBLIC ui milkdrop m)
