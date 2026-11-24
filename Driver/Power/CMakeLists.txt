# The C power management drivers (Internal/powerDr.h).  Only the host's
# (Host/, on GAPI gapi_power_status) so far; the asm drivers (APM,
# NoPower, the palmtops') stay for the 16-bit build.  The protocol is
# POWER_PROTO of powerDr.def (3.0).  Geodes need gcc or clang (module
# loading).
if(NOT GEOS_BUILD_GEODES)
  return()
endif()

add_subdirectory(Host)
