include_guard(GLOBAL)
add_library(util STATIC "${CMAKE_CURRENT_LIST_DIR}/json.c")
target_include_directories(util PUBLIC "${STROOM_ROOT}/src/common")
