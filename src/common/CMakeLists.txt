add_subdirectory(platform)
add_subdirectory(audio)
add_subdirectory(milkdrop)
add_subdirectory(ui)

target_compile_definitions(ui PUBLIC
  STROOM_PRESET_BENCHMARK=$<BOOL:${STROOM_PRESET_BENCHMARK}>
  STROOM_BENCHMARK_PROFILE=$<BOOL:${STROOM_BENCHMARK_PROFILE}>
)
target_compile_definitions(milkdrop PRIVATE
  STROOM_PRESET_BENCHMARK=$<BOOL:${STROOM_PRESET_BENCHMARK}>
  STROOM_BENCHMARK_PROFILE=$<BOOL:${STROOM_BENCHMARK_PROFILE}>
)
