# The Diconix 9-jet printer driver (diconix9.geo) in C: diconix9Manager.asm's
# PrintCom files (cursor by tabs in 1/288", MX graphics) and its control
# codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../DotMatrix.cmake)
geos_dotmatrix_driver(diconix9 GP diconix932.gp REV diconix9.rev
  FONTS Text/Font/fontDiconixInfo.asm
  PRINTCOM
    ${DOTMATRIX_BASE}
    ${DOTMATRIX_EPSON_JOB}
    ${DOTMATRIX_NO_COLOR}
    ${DOTMATRIX_9PIN_CURSOR}
    ${DOTMATRIX_MX_GRAPHICS}
    printcomCountryDialog.c
    Cursor/cursorSetCursorTab72.c
    Cursor/cursorConvert288.c
    Text/textPrintStyleRun.c
    Text/textLoadEpsonSymbolSet.c
    printcomEpsonStyles.c
  SOURCES diconix9ControlCodes.c)
set(diconix9_GEO ${diconix9_GEO} PARENT_SCOPE)
