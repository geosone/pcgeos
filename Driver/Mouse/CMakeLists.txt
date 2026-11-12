# The C mouse drivers: the common part (mouseCommon.c, beside
# mouseCommon.asm) and a device part per driver.  Only the host pointer
# (Host/, on ghal_input) so far.  The protocol is MOUSE_PROTO of
# mouseDr.def (3.2), as the DOS16 drivers have it.  Geodes need gcc or
# clang (module loading).
include(${PROJECT_SOURCE_DIR}/cmake/GeosGoc.cmake)
find_package(Python3 COMPONENTS Interpreter)
if(NOT (GEOS_GOC AND Python3_Interpreter_FOUND AND
        CMAKE_C_COMPILER_ID MATCHES "GNU|Clang"))
  return()
endif()
include(${PROJECT_SOURCE_DIR}/cmake/GeosGeode.cmake)

geos_geode(hostmse ${CMAKE_CURRENT_SOURCE_DIR}/mouseCommon.c
           ${CMAKE_CURRENT_SOURCE_DIR}/Host/hostMouse.c
           GP Host/hostmse.gp PROTOCOL 3.2
           INCLUDES ${CMAKE_CURRENT_SOURCE_DIR})
set(hostmse_GEO ${hostmse_GEO} PARENT_SCOPE)
