# The file stream driver (filestr.geo) in C: the port the spooler
# prints to a file through (Library/Spool/Process/processFile.c).
if(NOT GEOS_BUILD_GEODES)
  return()
endif()

# protocol: STREAM_PROTO (streamDr.def), DRIVER_PROTO + 1.0
geos_geode(filestr ${CMAKE_CURRENT_SOURCE_DIR}/filestrMain.c
           GP filestr32.gp PROTOCOL 3.0)
