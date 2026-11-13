# The Epson 9-pin printer driver (epson9.geo) in C: epson9Manager.asm's
# PrintCom files (FX cursor, text and graphics) and its control codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../DotMatrix.cmake)
geos_dotmatrix_driver(epson9 GP epson932.gp REV epson9.rev
  FONTS Text/Font/fontEpsonFXInfo.asm
  PRINTCOM ${DOTMATRIX_EPSON_COMMON}
    Cursor/cursorPrFormFeed72.c Cursor/cursorConvert216.c
    Cursor/cursor1ScanlineFeed.c
    Graphics/graphicsPrintSwath216.c Graphics/graphicsHi8IntXY.c
    Graphics/Rotate/rotate2pass8.c Graphics/graphicsMed8Int3Y.c
    Graphics/Rotate/rotate3x4.c
  SOURCES epson9ControlCodes.c)
set(epson9_GEO ${epson9_GEO} PARENT_SCOPE)
