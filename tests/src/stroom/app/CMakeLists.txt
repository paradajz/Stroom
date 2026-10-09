stroom_test(app_controller app test_app_controller.c)
stroom_test(app-config app test_config.c)
target_compile_definitions(test-app-config PRIVATE _DEFAULT_SOURCE)

stroom_test(app-artwork app test_artwork.c "${STROOM_ROOT}/src/stroom/app/artwork.c")
target_include_directories(test-app-artwork PRIVATE ../../common/ui/shared/stubs)
target_link_options(test-app-artwork PRIVATE "-Wl,--wrap=free")

stroom_test(app-runtime app test_runtime.c "${STROOM_ROOT}/src/stroom/app/runtime.c")
target_include_directories(test-app-runtime PRIVATE ../../common/ui/shared/stubs)
set_tests_properties(app-runtime PROPERTIES TIMEOUT 10)

# Compile the real startup/cleanup loop with a separate entry point for host tests.
add_library(app-main-fixture OBJECT "${STROOM_ROOT}/src/stroom/main.c")
target_link_libraries(app-main-fixture PRIVATE app)
target_include_directories(app-main-fixture PRIVATE ../../common/ui/shared/stubs)
target_compile_definitions(app-main-fixture PRIVATE main=stroom_main)
stroom_test(app-main app test_main.c $<TARGET_OBJECTS:app-main-fixture>)
target_include_directories(test-app-main PRIVATE ../../common/ui/shared/stubs)
target_link_options(test-app-main PRIVATE "-Wl,--wrap=fprintf")
set_tests_properties(app-main PROPERTIES TIMEOUT 10)
