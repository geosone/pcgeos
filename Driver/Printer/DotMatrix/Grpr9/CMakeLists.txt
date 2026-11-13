# The IBM Graphics 9-pin printer driver (grpr9.geo) in C: grpr9Manager.asm's PrintCom
# files (the IBM job, MX cursor by tabs, color, FX graphics, its styles) and its control codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../DotMatrix.cmake)
geos_dotmatrix_driver(grpr9 GP grpr932.gp REV grpr9.rev
  FONTS Text/Font/fontIBMGrprInfo.asm
  PRINTCOM
    ${DOTMATRIX_BASE}
    ${DOTMATRIX_IBM_JOB}
    ${DOTMATRIX_EPSON_COLOR}
    ${DOTMATRIX_9PIN_CURSOR}
    ${DOTMATRIX_FX_GRAPHICS}
    printcomPaperOnlyDialog.c
    Cursor/cursorSetCursorTab72.c
    Cursor/cursorConvert216.c
    Text/textPrintStyleRun.c
    Text/textLoadNoISOSymbolSet.c
  SOURCES grpr9ControlCodes.c grpr9Styles.c)
set(grpr9_GEO ${grpr9_GEO} PARENT_SCOPE)
