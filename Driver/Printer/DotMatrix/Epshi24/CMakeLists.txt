# The Late Model Epson 24-pin printer driver (epshi24.geo) in C: epshi24Manager.asm's
# PrintCom files (the LQ2 cursor and graphics, 360 x 360 dpi in two interleaves) and its
# control codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../DotMatrix.cmake)
geos_dotmatrix_driver(epshi24 GP epshi2432.gp REV epshi24.rev
  FONTS Text/Font/fontEpsonLQInfo.asm
  PRINTCOM
    ${DOTMATRIX_LQ2_COMMON}
    ${DOTMATRIX_LQ2_GRAPHICS}
  SOURCES epshi24ControlCodes.c)
set(epshi24_GEO ${epshi24_GEO} PARENT_SCOPE)
