# The IBM Proprinter 9-pin printer driver (prop9.geo) in C: prop9Manager.asm's
# PrintCom files (the IBM job and styles, the Epson MX cursor, the Epson FX
# graphics) and its control codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../DotMatrix.cmake)
geos_dotmatrix_driver(prop9 GP prop932.gp REV prop9.rev
  FONTS Text/Font/fontIBM9Info.asm
  PRINTCOM
    ${DOTMATRIX_IBM_COMMON}
    ${DOTMATRIX_PAGE}
    ${DOTMATRIX_EPSON_COLOR}
    ${DOTMATRIX_9PIN_CURSOR}
    ${DOTMATRIX_FX_GRAPHICS}
    Graphics/graphicsEpsonCommon.c
    Cursor/cursorSetCursorTab72.c
    Cursor/cursorConvert216.c
    printcomIBMStyles.c
  SOURCES prop9ControlCodes.c)
set(prop9_GEO ${prop9_GEO} PARENT_SCOPE)
