# The Okidata 9-pin printer driver (oki9.geo) in C: oki9Manager.asm's PrintCom
# files (the IBM job, tabs and numbers in ASCII, 7-pin graphics with ETX
# doubled, 72 x 144 dpi) and its control codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../DotMatrix.cmake)
geos_dotmatrix_driver(oki9 GP oki932.gp REV oki9.rev
  FONTS Text/Font/fontOkiInfo.asm
  PRINTCOM
    ${DOTMATRIX_CORE}
    ${DOTMATRIX_IBM_JOB}
    ${DOTMATRIX_PAGE}
    ${DOTMATRIX_NO_COLOR}
    printcomPaperOnlyDialog.c
    Text/textPrintStyleRun.c
    Text/textLoadNoISOSymbolSet.c
    Stream/streamHexToASCIILeading0.c
    Buffer/bufferOkiSendOutput.c
    Cursor/cursorSetCursorTab72ASCII.c
    Cursor/cursorPrLineFeedSet.c
    Cursor/cursorPrFormFeed72ASCII.c
    Cursor/cursorConvert144.c
    Cursor/cursor1ScanlineFeed.c
    Graphics/graphics2Resolutions.c
    Graphics/graphicsPrintSwath144.c
    Graphics/graphicsHi7IntY.c
    Graphics/graphicsLo7.c
    Graphics/Rotate/rotate7Back.c
  SOURCES oki9ControlCodes.c oki9Styles.c)
set(oki9_GEO ${oki9_GEO} PARENT_SCOPE)
