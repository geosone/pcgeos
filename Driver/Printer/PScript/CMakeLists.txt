# The PostScript printer driver (pscript.geo) in C: the common printer
# driver code of PrintCom/ (beside its asm) and this driver's own.  The
# device tables come from the asm (psdevinfo2c, Tools/psdevinfo2c).
if(NOT GEOS_BUILD_GEODES)
  return()
endif()

set(pc ${PROJECT_SOURCE_DIR}/Driver/Printer/PrintCom)
set(ps ${CMAKE_CURRENT_SOURCE_DIR})

# the devices: pscriptDriverInfo.asm and the info resources
file(GLOB pscript_infos ${ps}/*Info.asm)
list(REMOVE_ITEM pscript_infos ${ps}/pscriptDriverInfo.asm)
list(SORT pscript_infos)
set(pscript_devices ${CMAKE_CURRENT_BINARY_DIR}/pscriptDevices.c)
add_custom_command(OUTPUT ${pscript_devices}
  COMMAND ${GEOS_PSDEVINFO2C}
          -o ${pscript_devices} ${ps}/pscriptDriverInfo.asm ${pscript_infos}
  DEPENDS ${ps}/pscriptDriverInfo.asm ${pscript_infos}
          ${GEOS_PSDEVINFO2C} geos_host_tools
  COMMENT "psdevinfo2c pscript"
  VERBATIM)

# protocol: PRINT_PROTO (printDr.def), DRIVER_EXT_PROTO + 2.0
geos_geode(pscript
           ${pc}/printcomEntry.c ${pc}/printcomInfo.c ${pc}/printcomStream.c
           ${pc}/printcomNoText.c ${pc}/printcomNoStyles.c
           ${pc}/Color/colorGetFormat.c ${pc}/Color/colorSetNone.c
           ${pc}/printcomPScriptJob.c ${pc}/printcomPScriptDialog.c
           ${pc}/Job/jobPaperInfo.c ${pc}/Job/jobPaperPathNoASFControl.c
           ${ps}/pscriptAdmin.c ${ps}/pscriptControlCodes.c
           ${ps}/pscriptCursor.c ${ps}/pscriptGraphics.c ${ps}/pscriptPage.c
           ${ps}/pscriptPDL.c ${ps}/pscriptTables.c ${ps}/pscriptUtils.c
           ${pscript_devices}
           GP pscript32.gp REV pscript.rev PROTOCOL 5.0
           INCLUDES ${ps} ${pc} ${PROJECT_SOURCE_DIR}/CInclude/Internal)
