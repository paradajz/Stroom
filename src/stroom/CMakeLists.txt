add_subdirectory(app)
add_executable(stroom main.c app/runtime.c app/artwork.c recognition/client.c)
target_link_libraries(stroom PRIVATE app)
