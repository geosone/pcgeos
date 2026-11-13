# The Epson LX 9-pin printer driver (eplx9.geo) in C: eplx9Manager.asm's PrintCom
# files (MX cursor by tabs, FX text, color, 240 x 216 dpi graphics) and its control codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../DotMatrix.cmake)
geos_dotmatrix_driver(eplx9 GP eplx932.gp REV eplx9.rev
  FONTS Text/Font/fontEpsonFXInfo.asm
  PRINTCOM
    ${DOTMATRIX_BASE}
    ${DOTMATRIX_EPSON_JOB}
    ${DOTMATRIX_EPSON_COLOR}
    ${DOTMATRIX_9PIN_CURSOR}
    ${DOTMATRIX_FX_GRAPHICS}
    printcomCountryDialog.c
    Cursor/cursorSetCursorTab72.c
    Cursor/cursorConvert216.c
    Text/textPrintStyleRun.c
    Text/textLoadEpsonSymbolSet.c
    printcomEpsonStyles.c
  SOURCES eplx9ControlCodes.c)
set(eplx9_GEO ${eplx9_GEO} PARENT_SCOPE)
