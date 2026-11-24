# The C mouse drivers: the common part (mouseCommon.c, beside
# mouseCommon.asm) and a device part per driver.  Only the host pointer
# (Host/, on ghal_input) so far.  The protocol is MOUSE_PROTO of
# mouseDr.def (3.2), as the DOS16 drivers have it. 
if(NOT GEOS_BUILD_GEODES)
  return()
endif()

# mouseCommon.c: what the mouse drivers share
add_subdirectory(Host)
