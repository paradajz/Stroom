include("${CMAKE_CURRENT_LIST_DIR}/portable.cmake")
target_sources(ui PRIVATE
  shared/style.c
  frame/frame.c
  shared/draw.c
  shared/logo.c
  shared/player_view.c
  shared/track_view.c
  cd_player/view.c
  network_player/view.c
  settings/view.c
  waiting/view.c
  visualizer/overlays.c
  visualizer/scene.c
  visualizer/color_curve.c
  shared/text.c
  artwork/image.c
  artwork/decode.c
  artwork/jpeg.c
  artwork/png.c
  artwork/view.c
  artwork/worker.c
)

target_include_directories(ui PUBLIC "${GSKIT}/include")
target_link_directories(ui PUBLIC "${GSKIT}/lib")
target_include_directories(ui SYSTEM PRIVATE "${PS2SDK}/ports/include")
find_library(PS2_JPEG_LIBRARY jpeg PATHS "${PS2SDK}/ports/lib" NO_DEFAULT_PATH REQUIRED)
find_library(PS2_PNG_LIBRARY png PATHS "${PS2SDK}/ports/lib" NO_DEFAULT_PATH REQUIRED)
find_library(PS2_ZLIB_LIBRARY NAMES z zlibstatic PATHS "${PS2SDK}/ports/lib" NO_DEFAULT_PATH REQUIRED)
target_link_libraries(ui PUBLIC ps2_sdk PRIVATE platform gskit dmakit "${PS2_JPEG_LIBRARY}" "${PS2_PNG_LIBRARY}" "${PS2_ZLIB_LIBRARY}")
set_target_properties(ui PROPERTIES C_EXTENSIONS ON)
