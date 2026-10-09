add_library(audio STATIC
  "${CMAKE_CURRENT_LIST_DIR}/common/metadata.c"
  "${CMAKE_CURRENT_LIST_DIR}/common/metadata_json.c"
  "${CMAKE_CURRENT_LIST_DIR}/network/ariacast/stream.c"
  "${CMAKE_CURRENT_LIST_DIR}/artwork/http.c"
  "${CMAKE_CURRENT_LIST_DIR}/network/ariacast/websocket.c"
  "${CMAKE_CURRENT_LIST_DIR}/network/ariacast/playback.c"
  "${CMAKE_CURRENT_LIST_DIR}/common/pcm.c"
  "${CMAKE_CURRENT_LIST_DIR}/source/source.c"
  "${CMAKE_CURRENT_LIST_DIR}/cd/cd_format.c"
  "${CMAKE_CURRENT_LIST_DIR}/cd/cd_transport.c"
  "${CMAKE_CURRENT_LIST_DIR}/cd/cd_pcm.c"
  "${CMAKE_CURRENT_LIST_DIR}/cd/cd_scan.c"
  "${CMAKE_CURRENT_LIST_DIR}/cd/cd_controller.c"
  "${CMAKE_CURRENT_LIST_DIR}/cd/cd_playback.c"
)

target_include_directories(audio PUBLIC "${STROOM_ROOT}/src/common")
target_link_libraries(audio PUBLIC m)

target_sources(audio PRIVATE "${CMAKE_CURRENT_LIST_DIR}/cd/lookup/toc.c")

if(STROOM_DIAGNOSTICS)
  target_sources(audio PRIVATE "${CMAKE_CURRENT_LIST_DIR}/network/ariacast/diagnostics/diagnostics.c")
  target_sources(audio PRIVATE "${CMAKE_CURRENT_LIST_DIR}/network/ariacast/diagnostics/diagnostic_capture.c")
endif()
