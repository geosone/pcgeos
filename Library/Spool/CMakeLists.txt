# The print spool library (spool.geo) in C: the queues and their threads,
# PDL printing, the file, custom and nothing ports, the printers of the
# ini file (spoolLocal.h).
if(NOT GEOS_BUILD_GEODES)
  return()
endif()

set(sp ${CMAKE_CURRENT_SOURCE_DIR})

# the pictures (cvtpcx)
add_subdirectory(Art)

# the page sizes of libTables.asm (papergen, Tools/papergen)
set(spool_paper ${CMAKE_CURRENT_BINARY_DIR}/libPaperTables.c)
add_custom_command(OUTPUT ${spool_paper}
  COMMAND ${GEOS_PAPERGEN}
          -o ${spool_paper} ${sp}/Lib/libTables.asm
  DEPENDS ${sp}/Lib/libTables.asm ${GEOS_PAPERGEN} geos_host_tools
  COMMENT "papergen spool"
  VERBATIM)
# the symbol set tables of lib*Tab.asm (symtabgen, Tools/symtabgen)
set(spool_sym ${CMAKE_CURRENT_BINARY_DIR}/libSymbolTables.c)
set(spool_symtabs ibm437Table=libIBM8bitTab.asm ibm850Table=libIBM850Tab.asm
    ibm860Table=libIBM860Tab.asm ibm863Table=libIBM863Tab.asm
    ibm865Table=libIBM865Tab.asm romanTable=libRoman8bitTab.asm
    windowsTable=libWindows8bitTab.asm venturaTable=libVentura8bitTab.asm
    latinTable=libLatin8bitTab.asm)
set(spool_symdeps)
foreach(_t ${spool_symtabs})
  string(REGEX REPLACE ".*=" "" _f ${_t})
  list(APPEND spool_symdeps ${sp}/Lib/${_f})
endforeach()
add_custom_command(OUTPUT ${spool_sym}
  COMMAND ${GEOS_SYMTABGEN}
          -o ${spool_sym} ${spool_symtabs}
  WORKING_DIRECTORY ${sp}/Lib
  DEPENDS ${spool_symdeps} ${GEOS_SYMTABGEN} geos_host_tools
  COMMENT "symtabgen spool"
  VERBATIM)
add_subdirectory(Process)
add_subdirectory(Lib)
geos_get_sources(_spool spool)
geos_get_sources(_spool_goc spool_goc)
# (the generated tables belong to Lib/; made here, where the geode is)
geos_geode(spool ${_spool} ${spool_paper} ${spool_sym}
           GP spool32.gp GOC ${_spool_goc} REV spool.rev
           INCLUDES ${sp} ${sp}/Lib ${PROJECT_SOURCE_DIR}/CInclude/Internal)
