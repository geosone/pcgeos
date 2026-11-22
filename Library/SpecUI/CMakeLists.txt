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
               ${c}/CUtils/copenAppAttDet.goc
               ${c}/CUtils/copenAppCommon.goc
               ${c}/CUtils/copenAppMisc.goc
               ${c}/CUtils/copenMoniker.goc
               ${c}/CComp/copenCtrlClass.goc
               ${c}/CComp/copenCtrlGeometry.goc
               ${c}/CComp/copenCtrlCommon.goc
               ${c}/CComp/copenGadgetArea.goc
               ${c}/CComp/copenTitleGroup.goc
               ${c}/CComp/copenReplyBar.goc
               ${c}/CComp/copenMenuItemGroup.goc
               ${c}/CButton/copenButtonBuild.goc
               ${c}/CButton/copenButtonClass.goc
               ${c}/CButton/copenButtonCommon.goc
               ${c}/CButton/copenButtonColor.goc
               ${c}/CItem/citemItemGroupClass.goc
               ${c}/CItem/citemItemGroupVeryCommon.goc
               ${c}/CItem/citemItemGroupCommon.goc
               ${c}/CItem/citemBooleanGroup.goc
               ${c}/CItem/citemItemClass.goc
               ${c}/CItem/citemItemCommon.goc
               ${c}/CItem/citemItemColor.goc
               ${c}/CItem/citemCheckedItem.goc
               ${c}/CItem/citemScrollList.goc
               ${c}/CItem/citemScrollableItem.goc
               ${c}/CButton/copenMenuButton.goc
               ${c}/CComp/copenMenuBar.goc
               ${c}/CWin/cwinMenu.goc
               ${c}/CWin/cwinPtr.goc
               ${c}/CWin/cwinClassMisc.goc
               ${c}/CWin/cwinScreen.goc
               ${c}/CWin/cwinField.goc
               ${c}/CWin/cwinClassCommonHigh.goc
               ${c}/CWin/cwinClassCommonMiddle.goc
               ${c}/CWin/cwinClassCommonLow.goc
               ${c}/CWin/cwinClassOther.goc
               ${c}/CWin/cwinClassCUAS.goc
               ${c}/CWin/cwinGeometry.goc
               ${c}/CWin/cwinExcl.goc
               ${c}/CWin/cwinMenuedWin.goc
               ${c}/CWin/cwinBase.goc
               ${c}/CWin/cwinPopup.goc
               ${c}/CWin/cwinDialog.goc
               ${c}/CWin/cwinWinIcon.goc
               ${c}/CWin/cwinDisplayControl.goc
               ${c}/CWin/cwinDisplay.goc
               ${c}/CWin/cwinGlyphDisplay.goc
               ${CMAKE_CURRENT_SOURCE_DIR}/Motif/Win/winDraw.goc
               ${c}/CSpec/cspecSystem.goc
               ${c}/CSpec/cspecScreen.goc
               ${c}/CSpec/cspecField.goc
               ${c}/CSpec/cspecApplication.goc
               ${c}/CSpec/cspecPrimary.goc
               ${c}/CSpec/cspecDisplay.goc
               ${c}/CSpec/cspecDisplayControl.goc
               ${c}/CSpec/cspecInteraction.goc
               ${c}/CSpec/cspecTrigger.goc
               ${c}/CSpec/cspecItem.goc
               ${c}/CSpec/cspecCUAS.goc
               ${c}/CSpec/cspecValue.goc
               ${c}/CSpec/cspecGlyphDisplay.goc
               ${c}/CGadget/copenGlyphDisplay.goc
               ${c}/CGadget/copenTextCommon.goc
               ${c}/CSpec/cspecPane.goc
               ${c}/CSpec/cspecContent.goc
               ${c}/CComp/copenContent.goc
               ${c}/CView/cviewScrollbar.goc
               ${c}/CView/cviewScrollbarSelect.goc
               ${c}/CView/cviewPane.goc
               ${c}/CView/cviewPaneGeometry.goc
               ${c}/CView/cviewPaneScroll.goc
               ${c}/CView/cviewPaneWindow.goc
               ${CMAKE_CURRENT_SOURCE_DIR}/Motif/View/viewScrollbarDraw.goc
           INCLUDES ${c}
           PROTOCOL 727.0)
set(motif_GEO ${motif_GEO} PARENT_SCOPE)
