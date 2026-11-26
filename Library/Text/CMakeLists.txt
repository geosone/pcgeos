# The text library (text.geo) in C, beside the asm: VisTextClass with
# the text of small objects (TextStorage/), the single character and
# paragraph attribute (TextAttr/), the lines (TextLine/), the selection
# with cursor, keys and mouse (TextSelect/) so far.
# text32.gp has the parameters; the 16-bit build keeps text.gp.
if(NOT GEOS_BUILD_GEODES)
  return()
endif()

# the pictures (cvtpcx)
add_subdirectory(Art)
add_subdirectory(Text)
add_subdirectory(TextStorage)
add_subdirectory(TextAttr)
add_subdirectory(TextLine)
add_subdirectory(TextRegion)
add_subdirectory(TextSelect)
geos_get_sources(_text_goc text_goc)
geos_geode(text GP text32.gp GOC ${_text_goc}
           INCLUDES ${CMAKE_CURRENT_SOURCE_DIR})
