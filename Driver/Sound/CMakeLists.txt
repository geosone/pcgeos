# The C sound drivers (Internal/soundDrv.h).  Only the host's (Host/, on
# ghal_audio) so far; the asm drivers (Standard, SoundBlaster, ...) stay
# for the 16-bit build.  The protocol is SOUND_PROTO of soundDrv.def
# (3.0).
if(NOT GEOS_BUILD_GEODES)
  return()
endif()

add_subdirectory(Host)
