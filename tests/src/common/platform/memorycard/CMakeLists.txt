stroom_test(ps2-memorycard unity::framework test_card.c "${STROOM_ROOT}/src/common/platform/memorycard/card.c")
target_include_directories(test-ps2-memorycard PRIVATE stubs "${STROOM_ROOT}/src/common")
target_compile_definitions(test-ps2-memorycard PRIVATE _DEFAULT_SOURCE usleep=card_test_usleep)

stroom_test(boot-path unity::framework test_path.c)
target_include_directories(test-boot-path PRIVATE "${STROOM_ROOT}/src/common")
