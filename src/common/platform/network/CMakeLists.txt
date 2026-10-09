add_subdirectory(rpc)

# smap.irx is the NETMAN driver; ps2ip-nm.irx is its TCP/IP stack.
set(network_module_sources)
stroom_embed_irx("${PS2SDK}/iop/irx/ps2dev9.irx" ps2dev9_irx network_module_sources)
stroom_embed_irx("${PS2SDK}/iop/irx/netman.irx" netman_irx network_module_sources)
stroom_embed_irx("${PS2SDK}/iop/irx/smap.irx" smap_netman_irx network_module_sources)
stroom_embed_irx("${PS2SDK}/iop/irx/ps2ip-nm.irx" ps2ip_netman_irx network_module_sources)

# Keep the child-directory generation rules attached to an explicit target.
# The parent platform target otherwise sees these files as source-only outputs.
add_custom_target(network_modules DEPENDS ${network_module_sources})
add_dependencies(platform network_modules)

target_sources(platform PRIVATE runtime.c socket_error.c socket_mode.c ${network_module_sources})
target_link_libraries(platform PRIVATE socket_rpc ps2ips)
