# The C.Itoh 9-pin printer driver (citoh9.geo) in C: citoh9Manager.asm's
# PrintCom files (the Epson job and styles, X absolute and numbers in ASCII,
# the ribbon colors, 160 x 144 dpi) and its control codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../DotMatrix.cmake)
geos_dotmatrix_driver(citoh9 GP citoh932.gp REV citoh9.rev
  FONTS Text/Font/fontCitohInfo.asm
  PRINTCOM
    ${DOTMATRIX_CORE}
    ${DOTMATRIX_EPSON_JOB}
    ${DOTMATRIX_PAGE}
    Color/colorSetNextCMYK.c
    Color/colorSetFirstCMYK.c
    Color/colorGetFormat.c
    Color/colorSet.c
    Color/colorIWMapRGBToCMYK.c
    Color/colorSetRibbon.c
    printcomCountryDialog.c
    Text/textPrintStyleRun.c
    Text/textLoadNoISOSymbolSet.c
    printcomEpsonStyles.c
    Stream/streamHexToASCIILeading0.c
    Buffer/bufferSendOutput.c
    Cursor/cursorSetCursorAbsPitch.c
    Cursor/cursorPrLineFeedSetASCII.c
    Cursor/cursorPrFormFeedGuess.c
    Cursor/cursorConvert144.c
    Cursor/cursor1ScanlineFeed.c
    Graphics/graphics2Resolutions.c
    Graphics/graphicsCitohCommon.c
    Graphics/graphicsPrintSwath144.c
    Graphics/graphicsHi8IntY.c
    Graphics/graphicsLo8.c
    Graphics/Rotate/rotate8Back.c
  SOURCES citoh9ControlCodes.c)
set(citoh9_GEO ${citoh9_GEO} PARENT_SCOPE)
