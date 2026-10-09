add_library(ui STATIC
  "${CMAKE_CURRENT_LIST_DIR}/shared/font.c"
  "${CMAKE_CURRENT_LIST_DIR}/shared/text_layout.c"
  "${CMAKE_CURRENT_LIST_DIR}/shared/level_meter.c"
  "${CMAKE_CURRENT_LIST_DIR}/settings/controller.c"
  "${CMAKE_CURRENT_LIST_DIR}/visualizer/controller.c"
  "${CMAKE_CURRENT_LIST_DIR}/cd_player/controller.c"
  "${CMAKE_CURRENT_LIST_DIR}/cd_player/navigation.c"
  "${CMAKE_CURRENT_LIST_DIR}/presentation.c"
  "${CMAKE_CURRENT_LIST_DIR}/motion.c"
)
target_include_directories(ui PUBLIC "${STROOM_ROOT}/src/common")
target_link_libraries(ui PUBLIC milkdrop audio)
