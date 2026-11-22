# The text library (text.geo) in C, beside the asm: VisTextClass with
# the text of small objects (TextStorage/), the replacement every change
# goes through, reading the text, the selection as offsets and the single character
# attribute so far.
# text32.gp has the parameters; the 16-bit build keeps text.gp.  Geodes
# need gcc or clang (module loading).
include(${PROJECT_SOURCE_DIR}/cmake/GeosGoc.cmake)
find_package(Python3 COMPONENTS Interpreter)
if(NOT (GEOS_GOC AND Python3_Interpreter_FOUND AND
        CMAKE_C_COMPILER_ID MATCHES "GNU|Clang"))
  return()
endif()
include(${PROJECT_SOURCE_DIR}/cmake/GeosGeode.cmake)

set(t ${CMAKE_CURRENT_SOURCE_DIR})
geos_geode(text GP text32.gp
           GOC ${t}/Text/textInstance.goc
               ${t}/Text/textMethodSet.goc
               ${t}/Text/textMethodGet.goc
               ${t}/Text/textMethodInstance.goc
               ${t}/TextStorage/tsExternal.goc
               ${t}/TextStorage/tsParams.goc
               ${t}/TextStorage/tsReference.goc
               ${t}/TextAttr/taRange.goc
               ${t}/TextAttr/taCharAttr.goc
               ${t}/TextSelect/tslMain.goc
           INCLUDES ${t})
set(text_GEO ${text_GEO} PARENT_SCOPE)
