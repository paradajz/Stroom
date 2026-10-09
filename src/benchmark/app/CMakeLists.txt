add_library(preset_benchmark STATIC
  "${STROOM_ROOT}/src/benchmark/measurement/benchmark.c"
  "${STROOM_ROOT}/src/benchmark/app/runtime.c"
)
target_include_directories(preset_benchmark PUBLIC "${STROOM_ROOT}/src/benchmark")
target_link_libraries(preset_benchmark PUBLIC ui milkdrop m)
