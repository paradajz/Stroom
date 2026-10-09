add_library(platform STATIC platform.c iop/modules.c time/clock.c time/sleep.c memory/cache.c input/pad.c thread/scheduler.c thread/worker.c graphics/display.c graphics/readback.c cd/drive.c audio/output.c)
target_include_directories(platform PUBLIC "${STROOM_ROOT}/src/common")
target_include_directories(platform PUBLIC "${GSKIT}/include")
target_link_directories(platform PUBLIC "${GSKIT}/lib")
target_link_libraries(platform PUBLIC ps2_sdk PRIVATE pad patches -ldebug gskit dmakit cdvd audsrv)
set_target_properties(platform PROPERTIES C_EXTENSIONS ON)

add_subdirectory(network)

set(sound_module_sources)
foreach(module IN ITEMS audsrv freesd)
  stroom_embed_irx("${PS2SDK}/iop/irx/${module}.irx" "${module}_irx" sound_module_sources)
endforeach()
target_sources(platform PRIVATE ${sound_module_sources})
