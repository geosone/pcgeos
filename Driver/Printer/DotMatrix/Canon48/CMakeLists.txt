# The Canon BJ-130 48-jet printer driver (canon48.geo) in C:
# canon48Manager.asm's PrintCom files (the Epson job, the PPDS styles, the X24
# graphics with 48 jets, 360 x 360 dpi) and its control codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../DotMatrix.cmake)
geos_dotmatrix_driver(canon48 GP canon4832.gp REV canon48.rev
  FONTS Text/Font/fontCanon48Info.asm Text/Font/fontIBMPPDSInfo.asm
  PRINTCOM
    ${DOTMATRIX_CORE}
    ${DOTMATRIX_EPSON_JOB}
    ${DOTMATRIX_PAGE}
    ${DOTMATRIX_NO_COLOR}
    ${DOTMATRIX_IBMX24_GRAPHICS}
    printcomPaperOnlyDialog.c
    Text/textPrintStyleRun.c
    Text/textLoadNoISOSymbolSet.c
    Cursor/cursorSetCursorAbs72.c
    Cursor/cursorPrLineFeedExe.c
    Cursor/cursorPrFormFeed72.c
    Cursor/cursorConvert360.c
    Graphics/graphicsPrintSwath72To360.c
    Graphics/graphicsHi48.c
    Graphics/Rotate/rotate48.c
    printcomIBMPPDS24Styles.c
  SOURCES canon48ControlCodes.c)
set(canon48_GEO ${canon48_GEO} PARENT_SCOPE)
