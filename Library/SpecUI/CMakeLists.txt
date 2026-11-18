# The Motif specific UI library (motif.geo) in C, beside the asm of
# CommonUI and Motif: the build routines (CommonUI/CSpec), the system
# screen and field objects, the initialization and utilities so far.
# Motif/motif32.gp has the parameters; the 16-bit build keeps motif.gp.
# Geodes need gcc or clang (module loading).
include(${PROJECT_SOURCE_DIR}/cmake/GeosGoc.cmake)
find_package(Python3 COMPONENTS Interpreter)
if(NOT (GEOS_GOC AND Python3_Interpreter_FOUND AND
        CMAKE_C_COMPILER_ID MATCHES "GNU|Clang"))
  return()
endif()
include(${PROJECT_SOURCE_DIR}/cmake/GeosGeode.cmake)

set(c ${CMAKE_CURRENT_SOURCE_DIR}/CommonUI)
geos_geode(motif GP Motif/motif32.gp
           GOC ${c}/CMain/cmainVariable.goc
               ${c}/CMain/cmainCode.goc
               ${c}/CUtils/copenUtils.goc
               ${c}/CUtils/cutilsPtrImages.goc
               ${c}/CUtils/copenSystem.goc
               ${c}/CWin/cwinScreen.goc
               ${c}/CWin/cwinField.goc
               ${c}/CSpec/cspecSystem.goc
               ${c}/CSpec/cspecScreen.goc
               ${c}/CSpec/cspecField.goc
           INCLUDES ${c}
           PROTOCOL 727.0)
set(motif_GEO ${motif_GEO} PARENT_SCOPE)
