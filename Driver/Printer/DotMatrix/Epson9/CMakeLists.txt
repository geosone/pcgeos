# The Epson 9-pin printer driver (epson9.geo) in C: the common printer
# driver code of PrintCom/ (beside its asm) for the raster drivers and
# this driver's own.  The device and font tables come from the asm
# (Port32/tools/dmdevinfo2c.py).  Geodes need gcc or clang (module
# loading).
include(${PROJECT_SOURCE_DIR}/cmake/GeosGoc.cmake)
find_package(Python3 COMPONENTS Interpreter)
if(NOT (GEOS_GOC AND Python3_Interpreter_FOUND AND
        CMAKE_C_COMPILER_ID MATCHES "GNU|Clang"))
  return()
endif()
include(${PROJECT_SOURCE_DIR}/cmake/GeosGeode.cmake)

set(pc ${PROJECT_SOURCE_DIR}/Driver/Printer/PrintCom)
set(e9 ${CMAKE_CURRENT_SOURCE_DIR})

# the devices: epson9DriverInfo.asm, the info resources, the fonts
file(GLOB epson9_infos ${e9}/epson9*Info.asm)
list(REMOVE_ITEM epson9_infos ${e9}/epson9DriverInfo.asm)
list(SORT epson9_infos)
set(epson9_devices ${CMAKE_CURRENT_BINARY_DIR}/epson9Devices.c)
add_custom_command(OUTPUT ${epson9_devices}
  COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Port32/tools/dmdevinfo2c.py
          -o ${epson9_devices} -D ${e9}/epson9Constant.def
          --fonts ${pc}/Text/Font/fontEpsonFXInfo.asm
          ${e9}/epson9DriverInfo.asm ${epson9_infos}
  DEPENDS ${e9}/epson9DriverInfo.asm ${epson9_infos} ${e9}/epson9Constant.def
          ${pc}/Text/Font/fontEpsonFXInfo.asm
          ${PROJECT_SOURCE_DIR}/Port32/tools/dmdevinfo2c.py
  COMMENT "dmdevinfo2c epson9"
  VERBATIM)

# the PrintCom files of epson9Manager.asm, in its order
set(epson9_printcom
    printcomEntry.c printcomInfo.c printcomAdmin.c printcomNoEscapes.c
    Job/jobStartDefeatPaperout.c Job/jobEndDotMatrix.c
    Job/jobPaperPathASFControl.c Job/jobPaperInfo.c
    Job/jobResetPrinterAndWait.c
    printcomCountryDialog.c
    Page/pageStartSetLength.c Page/pageEndLFSetLength.c
    Cursor/cursorDotMatrixCommon.c Cursor/cursorSetCursorAbs72.c
    Cursor/cursorPrLineFeedExe.c Cursor/cursorPrFormFeed72.c
    Cursor/cursorConvert216.c Cursor/cursor1ScanlineFeed.c
    Text/textPrintStyleRun.c Text/textPrintText.c Text/textPrintRaw.c
    Text/textSetFont.c Text/textGetLineSpacing.c Text/textSetLineSpacing.c
    Text/textSetSymbolSet.c Text/textLoadEpsonSymbolSet.c
    printcomEpsonStyles.c Styles/stylesGet.c Styles/stylesSet.c
    Styles/stylesTest.c
    printcomStream.c
    Buffer/bufferCreate.c Buffer/bufferDestroy.c Buffer/bufferLoadBand.c
    Buffer/bufferScanBand.c Buffer/bufferSendOutput.c
    Buffer/bufferClearOutput.c
    Color/colorSetNextCMYK.c Color/colorSetFirstCMYK.c
    Color/colorGetFormat.c Color/colorSet.c Color/colorMapRGBToCMYK.c
    Color/colorSetRibbon.c
    Graphics/graphicsCommon.c Graphics/graphics3Resolutions.c
    Graphics/graphicsEpsonCommon.c Graphics/graphicsPrintSwath216.c
    Graphics/graphicsHi8IntXY.c Graphics/Rotate/rotate2pass8.c
    Graphics/graphicsMed8Int3Y.c Graphics/Rotate/rotate3x4.c
    Graphics/graphicsLo8.c Graphics/Rotate/rotate8.c)
list(TRANSFORM epson9_printcom PREPEND ${pc}/)

# protocol: PRINT_PROTO (printDr.def), DRIVER_EXT_PROTO + 2.0
geos_geode(epson9
           ${epson9_printcom} ${e9}/epson9ControlCodes.c ${epson9_devices}
           GP epson932.gp REV epson9.rev PROTOCOL 5.0
           INCLUDES ${e9} ${pc} ${PROJECT_SOURCE_DIR}/CInclude/Internal)
set(epson9_GEO ${epson9_GEO} PARENT_SCOPE)
