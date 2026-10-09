stroom_test(text-layout ui test_text_layout.c)
stroom_test(level-meter ui test_level_meter.c)
stroom_test(motion ui test_motion.c)
stroom_test(text ui test_text.c
  "${STROOM_ROOT}/src/common/ui/shared/text.c"
  "${STROOM_ROOT}/src/common/ui/shared/style.c")
target_include_directories(test-text PRIVATE stubs)

stroom_test(waiting-view ui test_waiting_view.c
  "${STROOM_ROOT}/src/common/ui/waiting/view.c")
target_include_directories(test-waiting-view PRIVATE stubs)
