# uic -g: a gstring in Esp as goc (Tools/uic/outgoc.c): labels,
# conditions, numbers, vardata left out (uicgstr.ui, uicgstrbad.ui).
if(NOT GEOS_HAVE_GOC)
  return()
endif()
add_test(NAME uic.goc.gstring
         COMMAND ${CMAKE_COMMAND} -D UIC=${GEOS_UIC} -D UICPP=${GEOS_UICPP}
                 -D GOC=${GEOS_GOC} -D SRC=${CMAKE_CURRENT_SOURCE_DIR}
                 -D OUT=${CMAKE_CURRENT_BINARY_DIR}/out
                 -D TOP=${PROJECT_SOURCE_DIR}
                 -P ${PROJECT_SOURCE_DIR}/Port32/tools/uic-goc.cmake)
set_tests_properties(uic.goc.gstring PROPERTIES TIMEOUT 120)
