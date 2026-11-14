# The Diablo 630 daisy wheel printer driver (diablo.geo) in C:
# diabloManager.asm's PrintCom files (the Epson job, the text only: no
# graphics, the pitches of the wheels, bold, italic as the second color
# and underline, the X by an absolute tab, line feeds of 1/6", 1/48"
# units) and its control codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../DotMatrix/DotMatrix.cmake)
geos_dotmatrix_driver(diablo GP diablo32.gp REV diablo.rev
  INFOS diablo630Info.asm
  FONTS Text/Font/fontDaisyWheelInfo.asm
  PRINTCOM
    printcomEntry.c printcomInfo.c printcomAdmin.c printcomNoEscapes.c
    ${DOTMATRIX_EPSON_JOB}
    ${DOTMATRIX_PAGE}
    ${DOTMATRIX_NO_COLOR}
    printcomPaperOnlyDialog.c
    Stream/streamHexToASCIILeading0.c printcomStream.c
    printcomNoGraphics.c
    Styles/stylesGet.c Styles/stylesSet.c Styles/stylesTest.c
    Cursor/cursorDotMatrixCommon.c Cursor/cursorSetCursorTosh.c
    Cursor/cursorPrLineFeedDumb6LPI.c Cursor/cursorPrFormFeedGuess.c
    Cursor/cursorConvert48.c
    Text/textPrintStyleRun.c Text/textPrintText.c Text/textPrintRaw.c
    Text/textSetFont.c Text/textGetLineSpacing.c Text/textSetLineSpacing.c
    Text/textSetSymbolSet.c Text/textLoadNoISOSymbolSet.c
  SOURCES diabloControlCodes.c diabloStyles.c)
set(diablo_GEO ${diablo_GEO} PARENT_SCOPE)
