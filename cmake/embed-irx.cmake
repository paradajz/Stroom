# Convert an IRX binary to a C byte array and append it to the caller's source list.
function(stroom_embed_irx irx_file symbol source_list)
  set(generated_source "${GENERATED_DIR}/${symbol}.c")
  add_custom_command(
    OUTPUT "${generated_source}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GENERATED_DIR}"
    COMMAND "${BIN2C_EXECUTABLE}" "${irx_file}" "${generated_source}" "${symbol}"
    DEPENDS "${irx_file}" "${BIN2C_EXECUTABLE}"
    VERBATIM
  )
  list(APPEND ${source_list} "${generated_source}")
  set(${source_list} "${${source_list}}" PARENT_SCOPE)
endfunction()
