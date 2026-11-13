# The Star 9-pin printer driver (star9.geo) in C: star9Manager.asm's PrintCom
# files (cursor by tabs in 1/144", the Star SG graphics) and its control codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../DotMatrix.cmake)
geos_dotmatrix_driver(star9 GP star932.gp REV star9.rev
  FONTS Text/Font/fontStarInfo.asm
  PRINTCOM
    ${DOTMATRIX_BASE}
    ${DOTMATRIX_EPSON_JOB}
    ${DOTMATRIX_NO_COLOR}
    ${DOTMATRIX_9PIN_CURSOR}
    Graphics/graphicsPrintSwath144.c
    Graphics/graphicsHi8IntXY.c
    Graphics/Rotate/rotate2pass8.c
    Graphics/graphicsMed8IntY.c
    printcomCountryDialog.c
    Cursor/cursorSetCursorTab72.c
    Cursor/cursorConvert144.c
    Text/textPrintStyleRun.c
    Text/textLoadEpsonSymbolSet.c
    printcomEpsonStyles.c
  SOURCES star9ControlCodes.c)
set(star9_GEO ${star9_GEO} PARENT_SCOPE)
