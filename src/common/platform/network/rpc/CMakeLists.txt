include("${STROOM_ROOT}/cmake/ps2sdk-rpc.cmake")
stroom_prepare_socket_rpc(rpc_source)

set(rpc_iop_source "${rpc_source}/iop/tcpip/tcpips/src")
set(rpc_ee_source "${rpc_source}/ee/rpc/tcpips/src")
set(rpc_build "${CMAKE_CURRENT_BINARY_DIR}")

find_program(IOP_CC mipsel-none-elf-gcc HINTS "${PS2DEV}/iop/bin" REQUIRED)
find_program(IOP_STRIP mipsel-none-elf-strip HINTS "${PS2DEV}/iop/bin" REQUIRED)

set(iop_flags -D_IOP -DPS2IP_DNS -fno-builtin -G0 -msoft-float -mno-explicit-relocs
  "-I${PS2SDK}/iop/include" "-I${PS2SDK}/common/include" "-I${STROOM_ROOT}/src/common"
  "-I${rpc_iop_source}" "-I${GENERATED_DIR}")

# Global C flags belong to the EE toolchain; share only configuration-specific flags.
string(TOUPPER "${CMAKE_BUILD_TYPE}" iop_configuration)
separate_arguments(iop_config_flags NATIVE_COMMAND "${CMAKE_C_FLAGS_${iop_configuration}}")
list(APPEND iop_flags ${iop_config_flags} "-DSTROOM_DIAGNOSTICS=$<BOOL:${STROOM_DIAGNOSTICS}>")

file(GENERATE OUTPUT "${rpc_build}/imports.c"
  CONTENT "#include \"irx_imports.h\"\n#include \"imports.lst\"\n")

function(stroom_compile_rpc_object name source)
  add_custom_command(
    OUTPUT "${rpc_build}/${name}.o"
    COMMAND "${IOP_CC}" ${iop_flags} ${ARGN} -MD -MF "${rpc_build}/${name}.d"
      -MT "${rpc_build}/${name}.o" -c "${source}" -o "${rpc_build}/${name}.o"
    DEPENDS "${source}" "${IOP_CC}"
    DEPFILE "${rpc_build}/${name}.d"
    VERBATIM
  )
endfunction()

stroom_compile_rpc_object(server "${rpc_iop_source}/ps2ips.c")
stroom_compile_rpc_object(imports "${rpc_build}/imports.c" -fno-toplevel-reorder)

if(STROOM_DIAGNOSTICS)
  stroom_compile_rpc_object(driver_stats "${CMAKE_CURRENT_SOURCE_DIR}/driver_stats.c")
  stroom_compile_rpc_object(network_diagnostic "${STROOM_ROOT}/src/common/platform/network/diagnostic/bridge.c")

  set(diagnostic_objects "${rpc_build}/driver_stats.o" "${rpc_build}/network_diagnostic.o")
endif()

add_custom_command(
  OUTPUT "${rpc_build}/socket.irx"
  BYPRODUCTS "${rpc_build}/socket.elf" "${rpc_build}/socket.stripped.elf"
  COMMAND "${IOP_CC}" ${iop_flags} "-T${PS2SDK}/iop/startup/linkfile" -nostdlib -Wl,-dc,-r
    "${rpc_build}/server.o" "${rpc_build}/imports.o" ${diagnostic_objects} -o "${rpc_build}/socket.elf"
  COMMAND "${IOP_STRIP}" --strip-unneeded --remove-section=.pdr --remove-section=.comment
    --remove-section=.mdebug.abi32 --remove-section=.gnu.attributes
    -o "${rpc_build}/socket.stripped.elf" "${rpc_build}/socket.elf"
  COMMAND "${IOP_FIXUP}" --rb --irx1 --allow-zero-text -o "${rpc_build}/socket.irx" "${rpc_build}/socket.stripped.elf"
  DEPENDS "${rpc_build}/server.o" "${rpc_build}/imports.o" ${diagnostic_objects} "${PS2SDK}/iop/startup/linkfile"
    "${IOP_CC}" "${IOP_STRIP}" "${IOP_FIXUP}"
  VERBATIM
)

set(rpc_module_sources)
stroom_embed_irx("${rpc_build}/socket.irx" socket_irx rpc_module_sources)
add_library(socket_rpc OBJECT "${rpc_ee_source}/ps2ipc.c" ${rpc_module_sources})
target_include_directories(socket_rpc PRIVATE "${STROOM_ROOT}/src/common")
target_link_libraries(socket_rpc PRIVATE ps2_sdk)
set_target_properties(socket_rpc PROPERTIES C_EXTENSIONS ON)

# Custom IOP compilation is outside CMake's EE compilation database.
if(STROOM_DIAGNOSTICS)
  set(iop_clang_tidy "$ENV{CLANG_TIDY}")
  if(NOT iop_clang_tidy)
    set(iop_clang_tidy clang-tidy)
  endif()
  set(iop_lint_flags ${iop_flags})
  list(REMOVE_ITEM iop_lint_flags -mno-explicit-relocs)
  add_custom_target(lint-iop
    COMMAND "${iop_clang_tidy}" "${STROOM_ROOT}/src/common/platform/network/diagnostic/bridge.c"
      --quiet -- --target=mipsel-none-elf ${iop_lint_flags}
    WORKING_DIRECTORY "${STROOM_ROOT}"
    VERBATIM
  )
else()
  add_custom_target(lint-iop)
endif()
