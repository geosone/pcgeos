# The Epson 48-jet printer driver (epson48.geo) in C: epson48Manager.asm's
# PrintCom files (the LQ2 cursor, 360 x 360 dpi in one pass of 48) and its
# control codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../DotMatrix.cmake)
geos_dotmatrix_driver(epson48 GP epson4832.gp REV epson48.rev
  FONTS Text/Font/fontEpsonLQInfo.asm
  PRINTCOM
    ${DOTMATRIX_LQ2_COMMON}
    Graphics/graphicsHi48.c
    Graphics/Rotate/rotate48.c
    Graphics/graphicsMed24.c
    Graphics/Rotate/rotate24.c
  SOURCES epson48ControlCodes.c)
set(epson48_GEO ${epson48_GEO} PARENT_SCOPE)
