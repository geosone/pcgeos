# The C power management drivers (Internal/powerDr.h).  Only the host's
# (Host/, on GAPI gapi_power_status) so far; the asm drivers (APM,
# NoPower, the palmtops') stay for the 16-bit build.  The protocol is
# POWER_PROTO of powerDr.def (3.0).  Geodes need gcc or clang (module
# loading).
include(${PROJECT_SOURCE_DIR}/cmake/GeosGoc.cmake)
find_package(Python3 COMPONENTS Interpreter)
if(NOT (GEOS_GOC AND Python3_Interpreter_FOUND AND
        CMAKE_C_COMPILER_ID MATCHES "GNU|Clang"))
  return()
endif()
include(${PROJECT_SOURCE_DIR}/cmake/GeosGeode.cmake)

geos_geode(hostpwr ${CMAKE_CURRENT_SOURCE_DIR}/Host/hostPower.c
           GP Host/hostpwr.gp PROTOCOL 3.0)
set(hostpwr_GEO ${hostpwr_GEO} PARENT_SCOPE)
