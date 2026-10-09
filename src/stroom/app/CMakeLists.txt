add_library(app STATIC controller.c config.c)
target_include_directories(app PUBLIC "${STROOM_ROOT}/src/stroom")
target_link_libraries(app PUBLIC ui audio milkdrop)
