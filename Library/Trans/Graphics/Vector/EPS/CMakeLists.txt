# The PostScript translation library (eps.geo), Port32: the export side
# in C (Export/*.c beside the asm), as the PostScript printer driver uses
# it.  Its PostScript text stays in the asm resources (exportPSCode.asm,
# exportComments.asm, exportProlog.asm); Port32/tools/asmchars2c.py makes
# C arrays of it at build time, Port32/tools/epsfonts2c.py the font tables
# (exportFontTables.asm, exportMoreFonts.asm, Include/fontID.def).  Geodes
# need gcc or clang.
include(${PROJECT_SOURCE_DIR}/cmake/GeosGoc.cmake)
find_package(Python3 COMPONENTS Interpreter)
if(NOT (GEOS_GOC AND Python3_Interpreter_FOUND AND
        CMAKE_C_COMPILER_ID MATCHES "GNU|Clang"))
  return()
endif()
include(${PROJECT_SOURCE_DIR}/cmake/GeosGeode.cmake)

set(_eps ${CMAKE_CURRENT_SOURCE_DIR})
set(_eps_text ${_eps}/Export/exportPSCode.asm ${_eps}/Export/exportComments.asm
              ${_eps}/Export/exportProlog.asm)
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

geos_geode(eps
           ${_eps}/Export/exportMain.c ${_eps}/Export/exportHeader.c
           ${_eps}/Export/exportGString.c ${_eps}/Export/exportPath.c
           ${_eps}/Export/exportArc.c ${_eps}/Export/exportUtils.c
           ${_eps}/Export/exportText.c ${_eps}/Export/exportBitmap.c
           ${_eps}/Lib/libMain.c ${_eps_strings} ${_eps_fonts}
           GP eps32.gp REV eps.rev
           INCLUDES ${_eps}/Export ${CMAKE_CURRENT_BINARY_DIR})
set(eps_GEO ${eps_GEO} PARENT_SCOPE)
