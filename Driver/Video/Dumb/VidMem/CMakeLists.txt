# The memory video driver (vidmem.geo): bitmaps of GrCreateBitmap.
geos_sources(vidmem vidmemUtils.c)
add_subdirectory(Main)
add_subdirectory(CMYK)
geos_get_sources(_vidcom vidcom)
geos_get_sources(_vidmem vidmem)
geos_geode(vidmem ${_vidcom} ${_vidmem}
           GP vidmem.gp PROTOCOL 5.3
           INCLUDES ${PROJECT_SOURCE_DIR}/Driver/Video/VidCom
                    ${CMAKE_CURRENT_SOURCE_DIR})
