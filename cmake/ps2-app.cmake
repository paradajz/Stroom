# Apply the shared PS2 executable, packing and launch rules to the selected app.
function(stroom_configure_ps2_app target)
  set_target_properties(${target} PROPERTIES SUFFIX .elf C_EXTENSIONS ON)
  target_link_options(${target} PRIVATE
    "-T${PS2SDK}/ee/startup/linkfile"
    "-Wl,-zmax-page-size=128"
  )
  target_link_libraries(${target} PRIVATE platform c)

  find_program(PS2_PACKER_EXECUTABLE ps2-packer HINTS "${PS2DEV}/bin" REQUIRED)
  set(packed_elf "${CMAKE_BINARY_DIR}/${target}-packed.elf")
  add_custom_command(
    OUTPUT "${packed_elf}"
    COMMAND "${PS2_PACKER_EXECUTABLE}" "$<TARGET_FILE:${target}>" "${packed_elf}"
    DEPENDS ${target} "${PS2_PACKER_EXECUTABLE}"
    VERBATIM
  )
  add_custom_target(packed-elf ALL DEPENDS "${packed_elf}")

  find_program(PS2CLIENT_EXECUTABLE ps2client HINTS "${PS2DEV}/bin" REQUIRED)
  add_custom_target(reset
    COMMAND "${PS2CLIENT_EXECUTABLE}" -h "${PS2_IP}" reset
    USES_TERMINAL
  )

  set(run_dependencies packed-elf)
  if(target STREQUAL "stroom")
    add_custom_target(stage-config
      COMMAND "${CMAKE_COMMAND}" -DSOURCE=${STROOM_ROOT}/STROOM.DAT
        -DDESTINATION=$<TARGET_FILE_DIR:${target}>/STROOM.DAT
        -P "${STROOM_ROOT}/cmake/stage-config.cmake"
      VERBATIM
    )
    list(APPEND run_dependencies stage-config)
  endif()

  add_custom_target(run
    COMMAND "${PS2CLIENT_EXECUTABLE}" -h "${PS2_IP}" execee host:$<TARGET_FILE_NAME:${target}>
    DEPENDS ${run_dependencies}
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:${target}>"
    USES_TERMINAL
  )
endfunction()
