# The PostScript translation library (eps.geo), Port32: the export side
# in C (Export/*.c beside the asm), as the PostScript printer driver uses
# it.  Its PostScript text stays in the asm resources (exportPSCode.asm,
# exportComments.asm, exportProlog.asm); Port32/tools/asmchars2c.py makes
# C arrays of it at build time, Port32/tools/epsfonts2c.py the font tables
# (exportFontTables.asm, exportMoreFonts.asm, Include/fontID.def).  Geodes
# need gcc or clang.
if(NOT GEOS_BUILD_GEODES)
  return()
endif()

set(_eps ${CMAKE_CURRENT_SOURCE_DIR})
set(_eps_text ${_eps}/Export/exportPSCode.asm ${_eps}/Export/exportComments.asm
              ${_eps}/Export/exportProlog.asm
              ${_eps}/Export/exportType3Fonts.asm)
set(_eps_strings ${CMAKE_CURRENT_BINARY_DIR}/epsStrings.c)
add_custom_command(OUTPUT ${_eps_strings} ${CMAKE_CURRENT_BINARY_DIR}/epsStrings.h
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Port32/tools/asmchars2c.py
          -o ${_eps_strings} -H ${CMAKE_CURRENT_BINARY_DIR}/epsStrings.h
          ${_eps_text}
  DEPENDS ${_eps_text} ${PROJECT_SOURCE_DIR}/Port32/tools/asmchars2c.py
  COMMENT "asmchars2c eps"
  VERBATIM)

set(_eps_fonts ${CMAKE_CURRENT_BINARY_DIR}/epsFonts.c)
set(_eps_font_asm ${_eps}/Export/exportFontTables.asm
                  ${_eps}/Export/exportMoreFonts.asm)
add_custom_command(OUTPUT ${_eps_fonts}
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Port32/tools/epsfonts2c.py
          -o ${_eps_fonts} --fontid ${PROJECT_SOURCE_DIR}/Include/fontID.def
          --order ${_eps}/Export/exportFontTables.asm ${_eps_font_asm}
  DEPENDS ${_eps_font_asm} ${PROJECT_SOURCE_DIR}/Include/fontID.def
          ${PROJECT_SOURCE_DIR}/Port32/tools/epsfonts2c.py
  COMMENT "epsfonts2c"
  VERBATIM)

add_subdirectory(Export)
add_subdirectory(Lib)
geos_get_sources(_eps_c eps)
# (the generated strings and font tables belong to Export/; made here,
# where the geode is)
geos_geode(eps ${_eps_c} ${_eps_strings} ${_eps_fonts}
           GP eps32.gp REV eps.rev
           INCLUDES ${_eps}/Export ${CMAKE_CURRENT_BINARY_DIR})
