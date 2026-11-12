# The CUPS port driver (cupsDr.h): a stream driver the spooler opens as a
# custom port; it hands the printer driver's output to a CUPS queue of
# the host through gapi_print (Port32/GAPI-Concept.md).  New in Port32,
# no asm original.  Geodes need gcc or clang (module loading).
include(${PROJECT_SOURCE_DIR}/cmake/GeosGoc.cmake)
find_package(Python3 COMPONENTS Interpreter)
if(NOT (GEOS_GOC AND Python3_Interpreter_FOUND AND
        CMAKE_C_COMPILER_ID MATCHES "GNU|Clang"))
  return()
endif()
include(${PROJECT_SOURCE_DIR}/cmake/GeosGeode.cmake)

# protocol: STREAM_PROTO (streamDr.def), DRIVER_PROTO + 1.0
geos_geode(cups ${CMAKE_CURRENT_SOURCE_DIR}/cupsMain.c
           GP cups.gp PROTOCOL 3.0
           INCLUDES ${CMAKE_CURRENT_SOURCE_DIR})
set(cups_GEO ${cups_GEO} PARENT_SCOPE)
