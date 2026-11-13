# The PostScript translation library (eps.geo), Port32: the export side
# in C (Export/*.c beside the asm), as the PostScript printer driver uses
# it.  Its PostScript text stays in the asm resources (exportPSCode.asm,
# exportComments.asm, exportProlog.asm); Port32/tools/asmchars2c.py makes
# C arrays of it at build time.  Geodes need gcc or clang.
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

geos_geode(eps
           ${_eps}/Export/exportMain.c ${_eps}/Export/exportHeader.c
           ${_eps}/Export/exportGString.c ${_eps}/Export/exportPath.c
           ${_eps}/Export/exportArc.c ${_eps}/Export/exportUtils.c
           ${_eps}/Lib/libMain.c ${_eps_strings}
           GP eps32.gp REV eps.rev
           INCLUDES ${_eps}/Export ${CMAKE_CURRENT_BINARY_DIR})
set(eps_GEO ${eps_GEO} PARENT_SCOPE)
