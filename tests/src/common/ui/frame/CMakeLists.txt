set(frame_sources test_frame.c
  "${STROOM_ROOT}/src/common/ui/frame/frame.c"
  "${STROOM_ROOT}/src/common/ui/shared/draw.c"
  "${STROOM_ROOT}/src/common/ui/shared/logo.c"
  "${STROOM_ROOT}/src/common/ui/shared/player_view.c"
  "${STROOM_ROOT}/src/common/ui/shared/track_view.c"
  "${STROOM_ROOT}/src/common/ui/cd_player/view.c"
  "${STROOM_ROOT}/src/common/ui/network_player/view.c"
  "${STROOM_ROOT}/src/common/ui/settings/view.c"
  "${STROOM_ROOT}/src/common/ui/waiting/view.c"
  "${STROOM_ROOT}/src/common/ui/shared/style.c"
  "${STROOM_ROOT}/src/common/ui/visualizer/overlays.c")
foreach(name frame frame-profile)
  stroom_test(${name} ui ${frame_sources})
  target_include_directories(test-${name} PRIVATE ../shared/stubs)
endforeach()

target_compile_definitions(test-frame-profile PRIVATE STROOM_PRESET_BENCHMARK=1)
