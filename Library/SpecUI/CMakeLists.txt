# The Motif specific UI library (motif.geo) in C, beside the asm of
# CommonUI and Motif: the build routines (CommonUI/CSpec), the system
# screen and field objects, the initialization and utilities so far.
# Motif/motif32.gp has the parameters; the 16-bit build keeps motif.gp.
if(NOT GEOS_BUILD_GEODES)
  return()
endif()

add_subdirectory(CommonUI)
add_subdirectory(Motif)
