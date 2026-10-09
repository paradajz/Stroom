# Refresh runtime configuration on every launch, including after local removal.
if(EXISTS "${SOURCE}")
  configure_file("${SOURCE}" "${DESTINATION}" COPYONLY)
else()
  file(REMOVE "${DESTINATION}")
endif()
