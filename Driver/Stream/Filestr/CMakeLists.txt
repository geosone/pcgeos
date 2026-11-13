# The file stream driver (filestr.geo) in C: the port the spooler
# prints to a file through (Library/Spool/Process/processFile.c).
# Geodes need gcc or clang (module loading).
include(${PROJECT_SOURCE_DIR}/cmake/GeosGoc.cmake)
find_package(Python3 COMPONENTS Interpreter)
if(NOT (GEOS_GOC AND Python3_Interpreter_FOUND AND
        CMAKE_C_COMPILER_ID MATCHES "GNU|Clang"))
  return()
endif()
include(${PROJECT_SOURCE_DIR}/cmake/GeosGeode.cmake)

# protocol: STREAM_PROTO (streamDr.def), DRIVER_PROTO + 1.0
geos_geode(filestr ${CMAKE_CURRENT_SOURCE_DIR}/filestrMain.c
           GP filestr32.gp PROTOCOL 3.0)
set(filestr_GEO ${filestr_GEO} PARENT_SCOPE)
