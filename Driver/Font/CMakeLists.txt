# The C font drivers: TrueType (Driver/Font/TrueType), the FreeType 1
# engine and its GEOS adapter, which were C already, and the driver's
# entry (Main/mainManager.c beside mainManager.asm).  The protocol is
# FONT_PROTO of fontDr.def (3.0).  The code counts on an unsigned char
# (as HighC and ARM have it: GEOS characters 128-255).  The warnings of
# the engine are its own (-w).  Geodes need gcc or clang (module
# loading).
include(${PROJECT_SOURCE_DIR}/cmake/GeosGoc.cmake)
find_package(Python3 COMPONENTS Interpreter)
if(NOT (GEOS_GOC AND Python3_Interpreter_FOUND AND
        CMAKE_C_COMPILER_ID MATCHES "GNU|Clang"))
  return()
endif()
include(${PROJECT_SOURCE_DIR}/cmake/GeosGeode.cmake)

set(_tt ${CMAKE_CURRENT_SOURCE_DIR}/TrueType)
set(_ft ttapi ttcalc ttcmap ttfile ttgload ttinterp ttload ttmemory
        ttobjs ttraster ftxkern)
set(_ad ttacache ttadapter ttcharmapper ttchars ttinit ttmetrics ttpath
        ttwidths)
list(TRANSFORM _ft PREPEND ${_tt}/FreeType/)
list(TRANSFORM _ft APPEND .c)
list(TRANSFORM _ad PREPEND ${_tt}/Adapter/)
list(TRANSFORM _ad APPEND .c)
geos_geode(truetype ${_ft} ${_ad} ${_tt}/Main/mainManager.c
           GP TrueType/truetype.gp PROTOCOL 3.0
           INCLUDES ${_tt}/Adapter ${_tt}/FreeType ${_tt}/Main
           DEFINITIONS __GEOS__
           OPTIONS -funsigned-char -w)
set(truetype_GEO ${truetype_GEO} PARENT_SCOPE)
