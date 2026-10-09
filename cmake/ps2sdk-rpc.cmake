function(stroom_prepare_socket_rpc output)
  find_program(PATCH_EXECUTABLE patch REQUIRED)

  set(PS2SDK_SOURCE_DIR "${STROOM_ROOT}/third_party/ps2sdk")

  set(rpc_patch "${STROOM_ROOT}/patches/ps2sdk/socket-rpc.patch")
  set(rpc_inputs
    ee/rpc/tcpips/src/ps2ipc.c
    iop/tcpip/tcpips/src/ps2ips.c
    iop/tcpip/tcpips/src/imports.lst
    iop/tcpip/tcpips/src/irx_imports.h
    LICENSE
  )
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${rpc_patch}")
  file(SHA256 "${rpc_patch}" rpc_fingerprint)
  foreach(input IN LISTS rpc_inputs)
    set(source "${PS2SDK_SOURCE_DIR}/${input}")
    if(NOT EXISTS "${source}")
      message(FATAL_ERROR
        "PS2SDK submodule is missing. Run git submodule update --init --recursive. "
        "The app build does not download sources.")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(SHA256 "${source}" source_hash)
    string(APPEND rpc_fingerprint "${source_hash}")
  endforeach()
  string(SHA256 rpc_fingerprint "${rpc_fingerprint}")
  # Changed sources or patches get a fresh work copy; the SDK submodule stays pristine.
  set(rpc_source "${CMAKE_BINARY_DIR}/_deps/ps2sdk-rpc-${rpc_fingerprint}")
  if(NOT EXISTS "${rpc_source}/.patched")
    file(REMOVE_RECURSE "${rpc_source}")
    foreach(input IN LISTS rpc_inputs)
      get_filename_component(parent "${input}" DIRECTORY)
      file(MAKE_DIRECTORY "${rpc_source}/${parent}")
      configure_file("${PS2SDK_SOURCE_DIR}/${input}" "${rpc_source}/${input}" COPYONLY)
    endforeach()
    execute_process(
      COMMAND "${PATCH_EXECUTABLE}" -p1 --batch --forward --input "${rpc_patch}"
      WORKING_DIRECTORY "${rpc_source}"
      RESULT_VARIABLE patch_result
      OUTPUT_VARIABLE patch_output
      ERROR_VARIABLE patch_error
    )
    if(NOT patch_result EQUAL 0)
      message(FATAL_ERROR "PS2SDK socket patch failed:\n${patch_output}${patch_error}")
    endif()
    file(WRITE "${rpc_source}/.patched" "${rpc_fingerprint}\n")
  endif()
  set(${output} "${rpc_source}" PARENT_SCOPE)
endfunction()
