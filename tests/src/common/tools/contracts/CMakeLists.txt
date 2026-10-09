stroom_generated_test(shared-contracts audio "${CMAKE_CURRENT_SOURCE_DIR}/test_contracts.mjs"
  DEPENDS ${contract_inputs}
)

add_test(NAME contract-regeneration COMMAND "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_generation.mjs")
