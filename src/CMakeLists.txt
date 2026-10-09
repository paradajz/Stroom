cmake_minimum_required(VERSION 3.21)
project(stroom_apps LANGUAGES C)

include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/project-options.cmake")
include("${STROOM_ROOT}/cmake/sdk.cmake")
include("${STROOM_ROOT}/cmake/embed-irx.cmake")
include("${STROOM_ROOT}/cmake/ps2-app.cmake")

add_library(ps2_sdk INTERFACE)
target_include_directories(ps2_sdk INTERFACE
  "${PS2SDK}/ee/include"
  "${PS2SDK}/common/include"
)
target_link_directories(ps2_sdk INTERFACE "${PS2SDK}/ee/lib")

add_subdirectory(common)
add_subdirectory("${STROOM_APP}")
stroom_configure_ps2_app("${STROOM_APP}")
