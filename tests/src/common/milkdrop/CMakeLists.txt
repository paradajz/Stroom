foreach(test IN ITEMS music milk_audio runtime wave_render presets preset_trig feedback milk objects)
  stroom_test(${test} milkdrop "test_${test}.c")
endforeach()

stroom_test(runtime-spectrum milkdrop test_runtime.c runtime_spectrum_fixture.c)
target_compile_definitions(test-runtime-spectrum PRIVATE TEST_PRESET_COUNT=2)

stroom_generated_test(shape-geometry milkdrop "${CMAKE_CURRENT_SOURCE_DIR}/test_shape_geometry.mjs"
  DEPENDS "${STROOM_ROOT}/src/common/milkdrop/milk_objects.c"
)

target_link_options(test-shape-geometry PRIVATE -Wl,--wrap=preset_blend -Wl,--wrap=milk_object_triangle -Wl,--wrap=milk_object_line)

stroom_generated_test(feedback-tiles milkdrop "${CMAKE_CURRENT_SOURCE_DIR}/test_feedback_tiles.mjs"
  DEPENDS "${STROOM_ROOT}/src/common/milkdrop/feedback_tiles.c"
)
