# The C font drivers: TrueType (Driver/Font/TrueType), the FreeType 1
# engine and its GEOS adapter, which were C already, and the driver's
# entry (Main/mainManager.c beside mainManager.asm).  The protocol is
# FONT_PROTO of fontDr.def (3.0).  The code counts on an unsigned char
# (as HighC and ARM have it: GEOS characters 128-255).  The warnings of
# the engine are its own (-w).  Geodes need gcc or clang (module
# loading).
if(NOT GEOS_BUILD_GEODES)
  return()
endif()

add_subdirectory(TrueType)
