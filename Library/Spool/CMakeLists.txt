# The print spool library (spool.geo) in C: the queues and their threads,
# PDL printing, the file, custom and nothing ports, the printers of the
# ini file (spoolLocal.h).  Geodes need gcc or clang (module loading).
include(${PROJECT_SOURCE_DIR}/cmake/GeosGoc.cmake)
find_package(Python3 COMPONENTS Interpreter)
if(NOT (GEOS_GOC AND Python3_Interpreter_FOUND AND
        CMAKE_C_COMPILER_ID MATCHES "GNU|Clang"))
  return()
endif()
include(${PROJECT_SOURCE_DIR}/cmake/GeosGeode.cmake)

set(sp ${CMAKE_CURRENT_SOURCE_DIR})

# the page sizes of libTables.asm (Port32/tools/papergen.py)
set(spool_paper ${CMAKE_CURRENT_BINARY_DIR}/libPaperTables.c)
add_custom_command(OUTPUT ${spool_paper}
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Port32/tools/papergen.py
          -o ${spool_paper} ${sp}/Lib/libTables.asm
  DEPENDS ${sp}/Lib/libTables.asm ${PROJECT_SOURCE_DIR}/Port32/tools/papergen.py
  COMMENT "papergen spool"
  VERBATIM)
geos_geode(spool
           ${sp}/Process/processLockQueue.c ${sp}/Process/processUtils.c
           ${sp}/Process/processQueue.c ${sp}/Process/processLoop.c
           ${sp}/Process/processThread.c ${sp}/Process/processPDL.c
           ${sp}/Process/processPort.c ${sp}/Process/processFile.c
           ${sp}/Process/processCustom.c ${sp}/Process/processNothing.c
           ${sp}/Process/processError.c ${sp}/Process/processRestart.c
           ${sp}/Lib/libPrinter.c ${sp}/Lib/libMisc.c ${sp}/Lib/libHost.c
           ${sp}/Lib/libPaper.c ${spool_paper}
           GP spool32.gp GOC ${sp}/Process/processApp.goc REV spool.rev
           INCLUDES ${sp} ${sp}/Lib ${PROJECT_SOURCE_DIR}/CInclude/Internal)
set(spool_GEO ${spool_GEO} PARENT_SCOPE)
