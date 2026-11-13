# The IBM Proprinter X24 24-pin printer driver (propx24.geo) in C:
# propx24Manager.asm's PrintCom files (the IBM job and styles, the X24 cursor
# and graphics, 360 x 180 dpi) and its control codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../DotMatrix.cmake)
geos_dotmatrix_driver(propx24 GP propx2432.gp REV propx24.rev
  FONTS Text/Font/fontIBM24Info.asm
  PRINTCOM
    ${DOTMATRIX_IBM_COMMON}
    ${DOTMATRIX_PAGE}
    ${DOTMATRIX_EPSON_COLOR}
    ${DOTMATRIX_IBMX24_GRAPHICS}
    Cursor/cursorSetCursorTab72.c
    Cursor/cursorPrLineFeedExe.c
    Cursor/cursorPrFormFeed60.c
    Cursor/cursorConvert180.c
    Graphics/graphicsPrintSwath72To180.c
    Graphics/graphicsHi24IntX.c
    Graphics/Rotate/rotate2pass24.c
    printcomIBMStyles.c
  SOURCES propx24ControlCodes.c)
set(propx24_GEO ${propx24_GEO} PARENT_SCOPE)
