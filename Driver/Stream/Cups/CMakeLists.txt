# The CUPS port driver (cupsDr.h): a stream driver the spooler opens as a
# custom port; it hands the printer driver's output to a CUPS queue of
# the host through gapi_print (Port32/GAPI-Concept.md).  New in Port32,
# no asm original.
if(NOT GEOS_BUILD_GEODES)
  return()
endif()

# protocol: STREAM_PROTO (streamDr.def), DRIVER_PROTO + 1.0
geos_geode(cups ${CMAKE_CURRENT_SOURCE_DIR}/cupsMain.c
           GP cups.gp PROTOCOL 3.0
           INCLUDES ${CMAKE_CURRENT_SOURCE_DIR})
