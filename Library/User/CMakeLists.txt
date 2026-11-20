# The user interface library (ui.geo) in C, beside the asm: the visible
# classes (Vis/), GenClass and the generic tree (Gen/), the Flow
# utilities, FlowClass and the input map (User/) so far.  ui32.gp has the parameters; the 16-bit build keeps ui.gp.  Geodes need gcc or clang (module loading).
include(${PROJECT_SOURCE_DIR}/cmake/GeosGoc.cmake)
find_package(Python3 COMPONENTS Interpreter)
if(NOT (GEOS_GOC AND Python3_Interpreter_FOUND AND
        CMAKE_C_COMPILER_ID MATCHES "GNU|Clang"))
  return()
endif()
include(${PROJECT_SOURCE_DIR}/cmake/GeosGeode.cmake)

set(ui ${CMAKE_CURRENT_SOURCE_DIR})
geos_geode(ui GP ui32.gp
           GOC ${ui}/Vis/visClass.goc ${ui}/Vis/visComp.goc
               ${ui}/Vis/visUtilsResident.goc
               ${ui}/Vis/visUtilsClass.goc
               ${ui}/Vis/visCompGeometry.goc
               ${ui}/Vis/visContent.goc
               ${ui}/Vis/visSpec.goc
               ${ui}/Gen/genUtils.goc
               ${ui}/Gen/genClass.goc
               ${ui}/Gen/genApplication.goc
               ${ui}/Gen/genSystem.goc
               ${ui}/Gen/genScreen.goc
               ${ui}/Gen/genField.goc
               ${ui}/Gen/genTrigger.goc
               ${ui}/Gen/genInteraction.goc
               ${ui}/Gen/genDisplay.goc
               ${ui}/Gen/genDisplayControl.goc
               ${ui}/Gen/genPrimary.goc
               ${ui}/Gen/genItemGroup.goc
               ${ui}/Gen/genItem.goc
               ${ui}/Gen/genBooleanGroup.goc
               ${ui}/Gen/genBoolean.goc
               ${ui}/Proc/procClass.goc
               ${ui}/User/userFlowUtils.goc
               ${ui}/User/userFlowMisc.goc
               ${ui}/User/userFlowInput.goc
               ${ui}/User/userMain.goc
               ${ui}/User/userScreen.goc
               ${ui}/User/userUtils.goc
               ${ui}/UI/uiMain.goc
               ${ui}/User/userLibrary.goc
           INCLUDES ${ui})
set(ui_GEO ${ui_GEO} PARENT_SCOPE)
