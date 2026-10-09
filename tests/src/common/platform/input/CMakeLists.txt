stroom_test(pad unity::framework test_pad.c "${STROOM_ROOT}/src/common/platform/input/pad.c")
target_include_directories(test-pad PRIVATE stubs "${STROOM_ROOT}/src/common" "${STROOM_ROOT}/third_party/ps2sdk/ee/rpc/pad/include")
target_compile_definitions(test-pad PRIVATE _DEFAULT_SOURCE)
