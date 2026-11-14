# The dumb ASCII printer driver (dumb.geo) in C: dumbManager.asm's
# PrintCom files (the Epson job, the text only: no graphics and no styles,
# the X by spaces, line feeds of 1/6", 1/48" units) and its control codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../DotMatrix/DotMatrix.cmake)
geos_dotmatrix_driver(dumb GP dumb32.gp REV dumb.rev
  INFOS dumbInfo.asm
  FONTS Text/Font/fontDumbInfo.asm
  PRINTCOM
    printcomEntry.c printcomInfo.c printcomAdmin.c printcomNoEscapes.c
    ${DOTMATRIX_EPSON_JOB}
    ${DOTMATRIX_PAGE}
    ${DOTMATRIX_NO_COLOR}
    printcomPaperOnlyDialog.c
    Stream/streamHexToASCIILeading0.c printcomStream.c
    printcomNoGraphics.c printcomNoStyles.c
    Cursor/cursorDotMatrixCommon.c Cursor/cursorSetCursorSpace72.c
    Cursor/cursorPrLineFeedDumb6LPI.c Cursor/cursorPrFormFeedGuess.c
    Cursor/cursorConvert48.c
    Text/textPrintStyleRunAddX.c Text/textPrintText.c Text/textPrintRaw.c
    Text/textSetFont.c Text/textGetLineSpacing.c Text/textSetLineSpacing.c
    Text/textSetSymbolSet.c Text/textLoadNoISOSymbolSet.c
  SOURCES dumbControlCodes.c)
set(dumb_GEO ${dumb_GEO} PARENT_SCOPE)
