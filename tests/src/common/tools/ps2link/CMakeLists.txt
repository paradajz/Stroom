stroom_test(ps2link-display-memory unity::framework test_display_memory.c
  "${STROOM_ROOT}/tools/ps2link/display_memory.c")
target_include_directories(test-ps2link-display-memory PRIVATE stubs)
target_compile_definitions(test-ps2link-display-memory PRIVATE
  memalign=launcher_test_memalign free=launcher_test_free)
