# Make loads the SDK environment before invoking CMake.
set(PS2SDK "$ENV{PS2SDK}" CACHE PATH "Source-built SDK" FORCE)
set(GSKIT "$ENV{GSKIT}" CACHE PATH "Source-built gsKit" FORCE)

set(BIN2C_EXECUTABLE "${PS2SDK}/bin/bin2c" CACHE FILEPATH "SDK binary converter" FORCE)
set(IOP_FIXUP "${PS2SDK}/bin/iopfixup" CACHE FILEPATH "SDK IOP fixup tool" FORCE)

foreach(library IN ITEMS PS2_JPEG_LIBRARY PS2_PNG_LIBRARY PS2_ZLIB_LIBRARY)
  unset(${library} CACHE)
endforeach()
