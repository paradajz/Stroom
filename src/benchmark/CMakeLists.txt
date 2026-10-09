add_subdirectory(app)
add_executable(benchmark main.c validation/color_curve.c validation/readback.c measurement/profile.c)
target_link_libraries(benchmark PRIVATE preset_benchmark)
