# The C video drivers: the common part (VidCom/*.c, beside the
# vidcom*.asm) and a device part per driver.  The host display
# (Host/, on ghal_display) and the memory video driver (Dumb/VidMem,
# into bitmaps).  The protocol is VIDEO_PROTO of
# videoDr.def (5.3), as the DOS16 drivers have it. 
if(NOT GEOS_BUILD_GEODES)
  return()
endif()

add_subdirectory(VidCom)
add_subdirectory(Host)
add_subdirectory(Dumb)
