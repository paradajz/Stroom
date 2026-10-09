set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR mips64r5900el)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(PS2DEV "$ENV{PS2DEV}" CACHE PATH "PS2 toolchain root" FORCE)

set(CMAKE_C_COMPILER "${PS2DEV}/ee/bin/mips64r5900el-ps2-elf-gcc")
set(CMAKE_AR "${PS2DEV}/ee/bin/mips64r5900el-ps2-elf-ar")
set(CMAKE_RANLIB "${PS2DEV}/ee/bin/mips64r5900el-ps2-elf-ranlib")
set(CMAKE_C_FLAGS_INIT "-D_EE -G0 -gdwarf-2 -gz")
