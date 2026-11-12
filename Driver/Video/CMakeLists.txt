# The C video drivers: the common part (VidCom/*.c, beside the
# vidcom*.asm) and a device part per driver.  The host display
# (Host/, on ghal_display) and the memory video driver (Dumb/VidMem,
# into bitmaps).  The protocol is VIDEO_PROTO of
# videoDr.def (5.3), as the DOS16 drivers have it.  Geodes need gcc or
# clang (module loading).
include(${PROJECT_SOURCE_DIR}/cmake/GeosGoc.cmake)
find_package(Python3 COMPONENTS Interpreter)
if(NOT (GEOS_GOC AND Python3_Interpreter_FOUND AND
        CMAKE_C_COMPILER_ID MATCHES "GNU|Clang"))
  return()
endif()
include(${PROJECT_SOURCE_DIR}/cmake/GeosGeode.cmake)

set(_vidcom ${CMAKE_CURRENT_SOURCE_DIR}/VidCom/vidcomEntry.c
            ${CMAKE_CURRENT_SOURCE_DIR}/VidCom/vidcomOutput.c
            ${CMAKE_CURRENT_SOURCE_DIR}/VidCom/vidcomLine.c
            ${CMAKE_CURRENT_SOURCE_DIR}/VidCom/vidcomRaster.c
            ${CMAKE_CURRENT_SOURCE_DIR}/VidCom/vidcomPolygon.c
            ${CMAKE_CURRENT_SOURCE_DIR}/VidCom/vidcomRegion.c
            ${CMAKE_CURRENT_SOURCE_DIR}/VidCom/vidcomPutLine.c
            ${CMAKE_CURRENT_SOURCE_DIR}/VidCom/vidcomChars.c
            ${CMAKE_CURRENT_SOURCE_DIR}/VidCom/vidcomXOR.c
            ${CMAKE_CURRENT_SOURCE_DIR}/VidCom/vidcomUnder.c
            ${CMAKE_CURRENT_SOURCE_DIR}/VidCom/vidcomPalette.c)
geos_geode(hostvid ${_vidcom}
           ${CMAKE_CURRENT_SOURCE_DIR}/Host/hostVideo.c
           ${CMAKE_CURRENT_SOURCE_DIR}/Host/hostPointer.c
           GP Host/hostvid.gp PROTOCOL 5.3
           INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/VidCom)
set(hostvid_GEO ${hostvid_GEO} PARENT_SCOPE)
# the memory video driver: bitmaps of GrCreateBitmap
set(_vidmem ${CMAKE_CURRENT_SOURCE_DIR}/Dumb/VidMem)
geos_geode(vidmem ${_vidcom}
           ${_vidmem}/Main/mainMain.c
           ${_vidmem}/vidmemUtils.c
           GP Dumb/VidMem/vidmem.gp PROTOCOL 5.3
           INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/VidCom ${_vidmem})
set(vidmem_GEO ${vidmem_GEO} PARENT_SCOPE)
