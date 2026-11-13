# The dot matrix printer drivers in C: the common printer driver code of
# PrintCom/ (beside its asm) and the driver's own files.  The device and
# font tables come from the asm (Port32/tools/dmdevinfo2c.py).  Geodes
# need gcc or clang (module loading).
#
# geos_dotmatrix_driver(<name> GP <gp> REV <rev> [FONTS <font info asm>...]
#                       [INFOS <info asm>...] [PREFIX <prefix>]
#                       PRINTCOM <PrintCom files> SOURCES <own files>)
# The driver's directory holds <prefix>DriverInfo.asm, its *Info.asm (or
# INFOS) and <prefix>Constant.def (the prefix is the name but where the
# asm files are named otherwise: toshiba24 for tosh24); the PCL drivers have no FONTS (their
# fonts are the printer's).  The result is ${<name>_GEO} in the parent
# scope.  The raster PCL drivers (Driver/Printer/HP) are built the same
# way.
include(${PROJECT_SOURCE_DIR}/cmake/GeosGoc.cmake)
find_package(Python3 COMPONENTS Interpreter)

function(geos_dotmatrix_driver name)
  cmake_parse_arguments(D "" "GP;REV;PREFIX" "FONTS;INFOS;PRINTCOM;SOURCES"
                        ${ARGN})
  if(NOT (GEOS_GOC AND Python3_Interpreter_FOUND AND
          CMAKE_C_COMPILER_ID MATCHES "GNU|Clang"))
    return()
  endif()
  include(${PROJECT_SOURCE_DIR}/cmake/GeosGeode.cmake)
  set(pc ${PROJECT_SOURCE_DIR}/Driver/Printer/PrintCom)
  set(pre ${name})			# the asm files' prefix
  if(D_PREFIX)
    set(pre ${D_PREFIX})
  endif()
  set(dir ${CMAKE_CURRENT_SOURCE_DIR})

  if(D_INFOS)
    list(TRANSFORM D_INFOS PREPEND ${dir}/ OUTPUT_VARIABLE infos)
  else()
    file(GLOB infos ${dir}/${pre}*Info.asm)
    list(REMOVE_ITEM infos ${dir}/${pre}DriverInfo.asm)
    list(SORT infos)
  endif()
  set(fonts)
  set(fontdep)
  foreach(f ${D_FONTS})
    list(APPEND fonts --fonts ${pc}/${f})
    list(APPEND fontdep ${pc}/${f})
  endforeach()
  set(devices ${CMAKE_CURRENT_BINARY_DIR}/${name}Devices.c)
  add_custom_command(OUTPUT ${devices}
    COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/Port32/tools/dmdevinfo2c.py
            -o ${devices} -D ${dir}/${pre}Constant.def
            ${fonts} ${dir}/${pre}DriverInfo.asm ${infos}
    DEPENDS ${dir}/${pre}DriverInfo.asm ${infos} ${dir}/${pre}Constant.def
            ${fontdep} ${PROJECT_SOURCE_DIR}/Port32/tools/dmdevinfo2c.py
    COMMENT "dmdevinfo2c ${name}"
    VERBATIM)

  list(TRANSFORM D_PRINTCOM PREPEND ${pc}/)
  list(TRANSFORM D_SOURCES PREPEND ${dir}/)
  # protocol: PRINT_PROTO (printDr.def), DRIVER_EXT_PROTO + 2.0
  geos_geode(${name} ${D_PRINTCOM} ${D_SOURCES} ${devices}
             GP ${D_GP} REV ${D_REV} PROTOCOL 5.0
             INCLUDES ${dir} ${pc} ${PROJECT_SOURCE_DIR}/CInclude/Internal)
  set(${name}_GEO ${${name}_GEO} PARENT_SCOPE)
endfunction()

# the PrintCom files of the dot matrix drivers, in the groups of the asm's
# printcom*.asm: what they all share (entry, info, the DotMatrix page,
# cursor basics, text, stream, buffer, the graphics' common part and the
# low resolution)
set(DOTMATRIX_CORE
    printcomEntry.c printcomInfo.c printcomAdmin.c printcomNoEscapes.c
    Cursor/cursorDotMatrixCommon.c
    Text/textPrintText.c Text/textPrintRaw.c
    Text/textSetFont.c Text/textGetLineSpacing.c Text/textSetLineSpacing.c
    Text/textSetSymbolSet.c
    Styles/stylesGet.c Styles/stylesSet.c Styles/stylesTest.c
    printcomStream.c
    Buffer/bufferCreate.c Buffer/bufferDestroy.c Buffer/bufferLoadBand.c
    Buffer/bufferScanBand.c Buffer/bufferClearOutput.c
    Graphics/graphicsCommon.c)
# the output as it is, three resolutions, the 8-pin low one
set(DOTMATRIX_STD
    Buffer/bufferSendOutput.c Graphics/graphics3Resolutions.c
    Graphics/graphicsLo8.c Graphics/Rotate/rotate8.c)
set(DOTMATRIX_PAGE
    Page/pageStartSetLength.c Page/pageEndLFSetLength.c)
set(DOTMATRIX_BASE
    ${DOTMATRIX_CORE} ${DOTMATRIX_STD} ${DOTMATRIX_PAGE}
    Graphics/graphicsEpsonCommon.c)
# printcomEpsonJob.asm, printcomIBMJob.asm
set(DOTMATRIX_EPSON_JOB
    Job/jobStartDefeatPaperout.c Job/jobEndDotMatrix.c
    Job/jobPaperPathASFControl.c Job/jobPaperInfo.c
    Job/jobResetPrinterAndWait.c)
set(DOTMATRIX_IBM_JOB
    Job/jobStartDotMatrix.c Job/jobEndDotMatrix.c
    Job/jobPaperPathNoASFControl.c Job/jobPaperInfo.c
    Job/jobResetPrinterAndWait.c)
# printcomEpsonColor.asm, printcomNoColor.asm
set(DOTMATRIX_EPSON_COLOR
    Color/colorSetNextCMYK.c Color/colorSetFirstCMYK.c
    Color/colorGetFormat.c Color/colorSet.c Color/colorMapRGBToCMYK.c
    Color/colorSetRibbon.c)
set(DOTMATRIX_NO_COLOR
    Color/colorSetNextMono.c Color/colorSetFirstMono.c
    Color/colorGetFormat.c Color/colorSetNone.c)
# the Epson 9-pin cursors (printcomEpson{FX,MX,RX}Cursor.asm): all but
# the X positioning and the units
set(DOTMATRIX_9PIN_CURSOR
    Cursor/cursorPrLineFeedExe.c Cursor/cursorPrFormFeed72.c
    Cursor/cursor1ScanlineFeed.c)
# the Epson 9-pin graphics: printcomEpsonFXGraphics.asm (240 x 216 dpi
# in two passes), printcomEpsonMXGraphics.asm (120 x 216)
set(DOTMATRIX_FX_GRAPHICS
    Graphics/graphicsPrintSwath216.c Graphics/graphicsHi8IntXY.c
    Graphics/Rotate/rotate2pass8.c Graphics/graphicsMed8Int3Y.c
    Graphics/Rotate/rotate3x4.c)
set(DOTMATRIX_MX_GRAPHICS
    Graphics/graphicsPrintSwath216.c Graphics/graphicsHi8IntY.c
    Graphics/graphicsMed8Int3Y.c Graphics/Rotate/rotate3x4.c)

# what the Epson drivers share (printcomEpsonJob, CountryDialog,
# DotMatrixPage, the FX cursor's X, the text, styles, stream, buffer and
# color files)
set(DOTMATRIX_EPSON_COMMON
    ${DOTMATRIX_BASE} ${DOTMATRIX_EPSON_JOB} ${DOTMATRIX_EPSON_COLOR}
    printcomCountryDialog.c
    Cursor/cursorSetCursorAbs72.c Cursor/cursorPrLineFeedExe.c
    Text/textPrintStyleRun.c Text/textLoadEpsonSymbolSet.c
    printcomEpsonStyles.c)

# the late model 24-pin and the 48-pin drivers: the Epson job, color,
# styles and text, the LQ2 cursor (printcomEpsonLQ2Cursor.asm: X
# absolute, line feeds by the spacing, 1/360") and the swath in 1/360";
# printcomEpsonLQ2Graphics.asm (360 dpi down in two interleaves of 24
# pins, each in two passes across)
set(DOTMATRIX_LQ2_COMMON
    ${DOTMATRIX_BASE} ${DOTMATRIX_EPSON_JOB} ${DOTMATRIX_EPSON_COLOR}
    printcomCountryDialog.c
    Cursor/cursorSetCursorAbs72.c Cursor/cursorPrLineFeedSet.c
    Cursor/cursorPrFormFeed60.c Cursor/cursorConvert360.c
    Text/textPrintStyleRun.c Text/textLoadEpsonSymbolSet.c
    printcomEpsonStyles.c
    Graphics/graphicsPrintSwath360.c)
set(DOTMATRIX_LQ2_GRAPHICS
    Graphics/graphicsHi24IntXY.c Graphics/Rotate/rotate2pass24.c
    Graphics/graphicsMed24.c Graphics/Rotate/rotate24.c)

# the IBM style drivers: the IBM job (custom entry and exit routines) and
# styles (overline), the graphics code ESC [ g with its byte count
# (graphicsIBMX24Common.c); the dialogs are printcomPaperOnlyDialog.c
set(DOTMATRIX_IBM_COMMON
    ${DOTMATRIX_CORE} ${DOTMATRIX_STD} ${DOTMATRIX_IBM_JOB}
    printcomPaperOnlyDialog.c
    Text/textPrintStyleRun.c Text/textLoadNoISOSymbolSet.c)
set(DOTMATRIX_IBMX24_GRAPHICS
    Graphics/graphicsIBMX24Common.c
    Graphics/graphicsMed24.c Graphics/Rotate/rotate24.c)
