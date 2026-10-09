set(audsrv_patch "${STROOM_ROOT}/patches/ps2sdk/audsrv-cleanup.patch")
set(audsrv_source "${CMAKE_CURRENT_BINARY_DIR}/sdk")
set(audsrv_inputs
  ee/rpc/audsrv/src/audsrv_rpc.c
  ee/rpc/audsrv/include/audsrv.h
  iop/sound/audsrv/src/audsrv.c
  iop/sound/audsrv/src/rpc_client.c
  iop/sound/audsrv/src/rpc_client.h
  iop/sound/audsrv/src/rpc_server.c
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${audsrv_patch}")
foreach(input IN LISTS audsrv_inputs)
  set(original "${STROOM_ROOT}/third_party/ps2sdk/${input}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${original}")
  configure_file("${original}" "${audsrv_source}/${input}" COPYONLY)
endforeach()
find_program(PATCH_EXECUTABLE patch REQUIRED)
execute_process(
  COMMAND "${PATCH_EXECUTABLE}" -p1 --batch --forward --input "${audsrv_patch}"
  WORKING_DIRECTORY "${audsrv_source}"
  RESULT_VARIABLE patch_result
  OUTPUT_VARIABLE patch_output
  ERROR_VARIABLE patch_error
)
if(NOT patch_result EQUAL 0)
  message(FATAL_ERROR "PS2SDK sound cleanup patch failed:\n${patch_output}${patch_error}")
endif()

foreach(side IN ITEMS ee iop)
  if(side STREQUAL ee)
    set(sources "${audsrv_source}/ee/rpc/audsrv/src/audsrv_rpc.c")
    set(headers "${audsrv_source}/ee/rpc/audsrv/include" "${STROOM_ROOT}/third_party/ps2sdk/ee/rpc/audsrv/src")
  else()
    set(sources "${audsrv_source}/iop/sound/audsrv/src/audsrv.c"
      "${audsrv_source}/iop/sound/audsrv/src/rpc_client.c"
      "${audsrv_source}/iop/sound/audsrv/src/rpc_server.c"
      "${STROOM_ROOT}/third_party/ps2sdk/iop/sound/audsrv/src/upsamplers.c"
      "${STROOM_ROOT}/third_party/ps2sdk/iop/sound/audsrv/src/common.c")
    set(headers "${STROOM_ROOT}/third_party/ps2sdk/iop/sound/audsrv/include" "${STROOM_ROOT}/third_party/ps2sdk/iop/sound/audsrv/src")
  endif()
  add_library(audsrv-${side}-fixture OBJECT ${sources})
  target_include_directories(audsrv-${side}-fixture PRIVATE sdk_stubs ${headers})
  # Compile entire SDK sources. Discard unused APIs at link time; no private
  # implementation state or extracted function bodies enter the tests.
  target_compile_options(audsrv-${side}-fixture PRIVATE -ffunction-sections -fdata-sections
    -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast)
  if(side STREQUAL iop)
    target_compile_definitions(audsrv-${side}-fixture PRIVATE _start=audsrv_module_start)
  endif()
  stroom_test(audsrv-${side}-cleanup audsrv-${side}-fixture test_audsrv_${side}.c)
  target_include_directories(test-audsrv-${side}-cleanup PRIVATE sdk_stubs ${headers})
  target_link_options(test-audsrv-${side}-cleanup PRIVATE -Wl,--gc-sections)
endforeach()
