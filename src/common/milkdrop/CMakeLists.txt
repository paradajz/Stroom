add_custom_target(compile-presets
  BYPRODUCTS
    "${GENERATED_DIR}/milk_presets.c"
    "${GENERATED_DIR}/milk_presets.h"
    "${GENERATED_DIR}/preset-report.json"
  COMMAND
    "${NODE_EXECUTABLE}"
    "${STROOM_ROOT}/tools/milkdrop/compile_presets.mjs"
    "${PRESET_ROOT}"
    "${MILKDROP_BENCHMARK_DIR}"
    "${GENERATED_DIR}"
    "${MILKDROP_PRESET_SELECTION}"
  DEPENDS
    ${MILKDROP_COMPILER_INPUTS}
    ${MILKDROP_BENCHMARK_INPUTS}
  COMMENT "Compiling measured MilkDrop equations to native C"
  VERBATIM
)

add_library(milkdrop STATIC
  runtime.c
  music.c
  milk_audio.c
  preset.c
  director.c
  milk_trig.c
  milk.c
  milk_wave_draw.c
  milk_effects.c
  milk_objects.c
  milk_object_draw.c
  feedback.c
  feedback_transition.c
  feedback_tiles.c
  "${GENERATED_DIR}/milk_presets.c"
)

add_dependencies(milkdrop compile-presets)
target_include_directories(milkdrop PUBLIC "${STROOM_ROOT}/src/common" "${GENERATED_DIR}")
target_link_libraries(milkdrop PUBLIC audio m)
