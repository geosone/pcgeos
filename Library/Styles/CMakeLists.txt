# The styles library (styles.geo) in C, beside the asm: the style sheet
# routines of Manip/ (the StyleSheetControlClass of UI/ comes with the
# controllers).  styles32.gp has the parameters; the 16-bit build keeps
# styles.gp.
# the pictures (cvtpcx; in every build, the ctest library.art.styles)
add_subdirectory(Art)
if(NOT GEOS_BUILD_GEODES)
  return()
endif()

add_subdirectory(Manip)
geos_get_sources(_styles_goc styles_goc)
# The strings of uiStyleSheet.ui (uic -g): ControlStrings so far.  The
# binary directory first: "Art/mkr*.ui" are then the pictures cvtpcx
# made (Art/CMakeLists.txt).
geos_uic(stylesUI ${CMAKE_CURRENT_SOURCE_DIR}/UI/uiManager.ui
         PROLOG ${CMAKE_CURRENT_SOURCE_DIR}/UI/uiManagerUI.goh
         RESOURCES ControlStrings
         INCLUDES ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/UI
         DEPENDS styles_art)
geos_geode(styles GP styles32.gp GOC ${_styles_goc} ${stylesUI_UI_GOC}
           GOC_DEPENDS ${stylesUI_UI_GOH}
           INCLUDES ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/UI
                    ${stylesUI_UI_DIR})
