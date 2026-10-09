# Configure-time generation also covers custom IOP commands and host tests.
file(GLOB contract_definitions CONFIGURE_DEPENDS
  "${STROOM_ROOT}/shared/contracts/*.json"
)
set(contract_inputs
  ${contract_definitions}
  "${STROOM_ROOT}/tools/contracts/load.mjs"
  "${STROOM_ROOT}/tools/contracts/generate.mjs"
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${contract_inputs})
if(NOT DEFINED STROOM_VIDEO_MODE)
  set(STROOM_VIDEO_MODE "480i")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "STROOM_VIDEO_MODE=${STROOM_VIDEO_MODE}"
  "${NODE_EXECUTABLE}" "${STROOM_ROOT}/tools/contracts/generate.mjs" "${GENERATED_DIR}/contracts"
  RESULT_VARIABLE contract_result)
if(NOT contract_result EQUAL 0)
  message(FATAL_ERROR "Shared contract generation failed")
endif()
include_directories("${GENERATED_DIR}")
