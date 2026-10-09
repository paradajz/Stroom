include("${STROOM_ROOT}/cmake/ps2sdk-rpc.cmake")
stroom_prepare_socket_rpc(rpc_source)

set(sdk "${STROOM_ROOT}/third_party/ps2sdk")
# Copy complete public headers so SDK system headers do not shadow the host libc.
configure_file("${sdk}/ee/libcglue/include/ps2sdkapi.h" "${CMAKE_CURRENT_BINARY_DIR}/sdk_headers/ps2sdkapi.h" COPYONLY)
configure_file("${sdk}/common/include/ps2ip_rpc.h" "${CMAKE_CURRENT_BINARY_DIR}/sdk_headers/ps2ip_rpc.h" COPYONLY)
# Use the SDK's normal per-function libcglue build flags for public socket APIs.
# SDK sources retain their PS2 pointer and socket signatures on the host.
add_library(socket-ee-fixture OBJECT
  "${rpc_source}/ee/rpc/tcpips/src/ps2ipc.c"
  "${sdk}/ee/rpc/tcpips/src/ps2ipc_ps2sdk.c"
  "${sdk}/ee/libcglue/src/socket.c")
target_include_directories(socket-ee-fixture PRIVATE sdk_stubs
  "${CMAKE_CURRENT_BINARY_DIR}/sdk_headers" "${sdk}/ee/rpc/tcpips/include"
  "${sdk}/ee/rpc/tcpips/src" "${STROOM_ROOT}/src/common")
target_compile_options(socket-ee-fixture PRIVATE
  "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/sdk_stubs/tcpip.h"
  "SHELL:-include fcntl.h" "SHELL:-include sys/ioctl.h"
  -ffunction-sections -fdata-sections
  -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -Wno-incompatible-pointer-types -Wno-pointer-sign -Wno-unused-parameter -Wno-sign-compare)
target_compile_definitions(socket-ee-fixture PRIVATE F_socket F_recv F_recvfrom _DEFAULT_SOURCE)

foreach(test IN ITEMS socket-rpc socket-bridge socket-lifecycle diagnostic-client)
  if(test STREQUAL diagnostic-client AND NOT STROOM_DIAGNOSTICS)
    continue()
  endif()
  if(test STREQUAL socket-rpc)
    set(source test_receive.c)
  elseif(test STREQUAL socket-bridge)
    set(source test_bridge.c)
  elseif(test STREQUAL socket-lifecycle)
    set(source test_lifecycle.c)
  else()
    set(source test_diagnostic_client.c)
  endif()
  stroom_test(${test} socket-ee-fixture ${source} ee_fixture.c)
  target_include_directories(test-${test} PRIVATE sdk_stubs
    "${CMAKE_CURRENT_BINARY_DIR}/sdk_headers" "${sdk}/ee/rpc/tcpips/include"
    "${sdk}/ee/libcglue/src" "${STROOM_ROOT}/src/common")
  target_compile_options(test-${test} PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/sdk_stubs/tcpip.h")
  target_compile_definitions(test-${test} PRIVATE _DEFAULT_SOURCE)
  target_link_options(test-${test} PRIVATE -Wl,--gc-sections)
endforeach()

if(STROOM_DIAGNOSTICS)
  stroom_test(driver-stats unity::framework test_driver_stats.c
    "${STROOM_ROOT}/src/common/platform/network/rpc/driver_stats.c")
  target_include_directories(test-driver-stats PRIVATE
    "${PROJECT_SOURCE_DIR}/src/common/stubs/socket_rpc/driver"
    "${STROOM_ROOT}/src/common" "${sdk}/common/include")
endif()

add_library(socket-iop-fixture OBJECT "${rpc_source}/iop/tcpip/tcpips/src/ps2ips.c")
target_include_directories(socket-iop-fixture PRIVATE sdk_stubs "${CMAKE_CURRENT_BINARY_DIR}/sdk_headers" "${STROOM_ROOT}/src/common")
target_compile_definitions(socket-iop-fixture PRIVATE _start=socket_module_start _DEFAULT_SOURCE)
target_compile_options(socket-iop-fixture PRIVATE -ffunction-sections -fdata-sections
  -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -Wno-incompatible-pointer-types -Wno-pointer-sign)
foreach(test IN ITEMS socket-rpc socket-bridge)
  target_link_libraries(test-${test} PRIVATE socket-iop-fixture)
  target_sources(test-${test} PRIVATE iop_fixture.c)
endforeach()
# EE and IOP export the same names on separate CPUs; keep them distinct on the host.
set(iop_symbols platform_net_driver_stats=rpc_peer_driver_stats
  platform_net_diagnostic=rpc_peer_diagnostic)
target_compile_definitions(socket-iop-fixture PRIVATE ${iop_symbols})
set_source_files_properties(iop_fixture.c PROPERTIES COMPILE_DEFINITIONS "${iop_symbols}")
