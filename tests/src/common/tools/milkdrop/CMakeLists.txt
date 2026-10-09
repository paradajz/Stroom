set(compiler_dependencies
  ${contract_inputs}
  ${MILKDROP_COMPILER_INPUTS}
)

foreach(test IN ITEMS nested-trig deferred-trig point-reuse)
  string(REPLACE "-" "_" script "${test}")
  stroom_generated_test(${test} milkdrop "${CMAKE_CURRENT_SOURCE_DIR}/test_${script}.mjs"
    DEPENDS ${compiler_dependencies}
  )
endforeach()

stroom_generated_test(point-invariants milkdrop "${CMAKE_CURRENT_SOURCE_DIR}/test_point_invariants.mjs"
  ARGS "${PRESET_ROOT}" "${MILKDROP_BENCHMARK_DIR}"
  DEPENDS ${compiler_dependencies} ${MILKDROP_BENCHMARK_INPUTS}
)

stroom_generated_test(preset-compiler m "${CMAKE_CURRENT_SOURCE_DIR}/test_compiler.mjs"
  DEPENDS ${compiler_dependencies}
)

stroom_generated_test(wave-point-loop milkdrop "${CMAKE_CURRENT_SOURCE_DIR}/test_wave_point_loop.mjs"
  ARGS "${PRESET_ROOT}" "${MILKDROP_BENCHMARK_DIR}"
  DEPENDS ${compiler_dependencies} ${MILKDROP_BENCHMARK_INPUTS} "${STROOM_ROOT}/src/common/milkdrop/milk_wave_points.h"
)
