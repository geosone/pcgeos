# The Toshiba 24-pin printer driver (tosh24.geo) in C: toshiba24Manager.asm's
# PrintCom files (the Epson job, tabs, line feeds in ASCII, columns of four
# six-pin bytes, 360 x 180 dpi) and its control codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../DotMatrix.cmake)
geos_dotmatrix_driver(tosh24 GP tosh2432.gp REV tosh24.rev PREFIX toshiba24
  FONTS Text/Font/fontToshibaInfo.asm
  PRINTCOM
    ${DOTMATRIX_CORE}
    ${DOTMATRIX_EPSON_JOB}
    ${DOTMATRIX_PAGE}
    ${DOTMATRIX_NO_COLOR}
    printcomCountryDialog.c
    Text/textPrintStyleRun.c
    Text/textLoadNoISOSymbolSet.c
    Stream/streamHexToASCIILeading0.c
    Buffer/bufferSendOutput.c
    Cursor/cursorSetCursorTosh.c
    Cursor/cursorPrLineFeedSetASCII.c
    Cursor/cursorPrFormFeedGuess.c
    Cursor/cursorConvert48.c
    Graphics/graphics2ResHigh.c
    Graphics/graphicsCitohCommon.c
    Graphics/graphicsPrintSwath48.c
    Graphics/graphicsHi24IntX.c
    Graphics/Rotate/rotate2pass24Into4.c
    Graphics/graphicsMed24.c
    Graphics/Rotate/rotate24Into4.c
  SOURCES toshiba24ControlCodes.c toshiba24Styles.c)
set(tosh24_GEO ${tosh24_GEO} PARENT_SCOPE)
