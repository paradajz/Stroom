stroom_test(scene_batch ui test_scene_batch.c)

stroom_test(wave-segments ui test_wave_segments.c
  "${STROOM_ROOT}/src/common/ui/visualizer/scene.c"
  "${STROOM_ROOT}/src/common/ui/shared/style.c")
target_include_directories(test-wave-segments PRIVATE stubs ../shared/stubs)
target_link_options(test-wave-segments PRIVATE -Wl,--wrap=preset_draw)

stroom_test(scene ui test_scene.c
  "${STROOM_ROOT}/src/common/ui/visualizer/scene.c"
  "${STROOM_ROOT}/src/common/ui/shared/style.c")
target_include_directories(test-scene PRIVATE stubs ../shared/stubs)
target_link_options(test-scene PRIVATE -Wl,--wrap=preset_draw -Wl,--wrap=milk_composite -Wl,--wrap=feedback_emit -Wl,--wrap=feedback_echo_emit)

stroom_test(color-curve-layout ui test_color_curve_layout.c)

stroom_test(scene-lifecycle ui test_scene_lifecycle.c
  "${STROOM_ROOT}/src/common/ui/visualizer/scene.c"
  "${STROOM_ROOT}/src/common/ui/shared/style.c")
target_include_directories(test-scene-lifecycle PRIVATE stubs ../shared/stubs)
