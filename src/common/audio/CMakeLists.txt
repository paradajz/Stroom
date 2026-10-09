include("${CMAKE_CURRENT_LIST_DIR}/portable.cmake")

target_sources(audio PRIVATE
  output/device.c
  network/ariacast/server.c
  network/ariacast/client.c
  network/ariacast/socket.c
  network/ariacast/discovery.c
  network/ariacast/statistics.c
  artwork/receiver.c
  network/network.c
  cd/cd.c
  cd/cd_output.c
)

target_link_libraries(audio PRIVATE platform ps2_sdk)
set_target_properties(audio PROPERTIES C_EXTENSIONS ON)

if(STROOM_DIAGNOSTICS)
  target_sources(audio PRIVATE network/ariacast/diagnostics/poll.c network/ariacast/diagnostics/requests.c)
endif()
