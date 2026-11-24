# The TrueType font driver (truetype.geo): FreeType 1 (FreeType/), its
# adapter to the font driver interface (Adapter/) and the entry (Main/).
add_subdirectory(FreeType)
add_subdirectory(Adapter)
add_subdirectory(Main)
geos_get_sources(_truetype truetype)
geos_geode(truetype ${_truetype}
           GP truetype.gp PROTOCOL 3.0
           INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/Adapter
                    ${CMAKE_CURRENT_SOURCE_DIR}/FreeType
                    ${CMAKE_CURRENT_SOURCE_DIR}/Main
           DEFINITIONS __GEOS__
           OPTIONS -funsigned-char -w)
