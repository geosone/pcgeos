# The Epson MX 9-pin printer driver (epmx9.geo) in C: epmx9Manager.asm's PrintCom
# files (MX cursor by tabs, MX text, 120 x 216 dpi graphics) and its control codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../DotMatrix.cmake)
geos_dotmatrix_driver(epmx9 GP epmx932.gp REV epmx9.rev
  FONTS Text/Font/fontEpsonMXInfo.asm
  PRINTCOM
    ${DOTMATRIX_BASE}
    ${DOTMATRIX_EPSON_JOB}
    ${DOTMATRIX_NO_COLOR}
    ${DOTMATRIX_9PIN_CURSOR}
    ${DOTMATRIX_MX_GRAPHICS}
    printcomCountryDialog.c
    Cursor/cursorSetCursorTab72.c
    Cursor/cursorConvert216.c
    Text/textPrintStyleRun.c
    Text/textLoadEpsonSymbolSet.c
    printcomEpsonStyles.c
  SOURCES epmx9ControlCodes.c)
set(epmx9_GEO ${epmx9_GEO} PARENT_SCOPE)
