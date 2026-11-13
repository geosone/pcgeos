# The IBM PPDS 24-pin printer driver (ppds24.geo) in C: ppds24Manager.asm's
# PrintCom files (the IBM job, the PPDS cursor, styles and graphics, 360 x 360
# dpi in two interleaves) and its control codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../DotMatrix.cmake)
geos_dotmatrix_driver(ppds24 GP ppds2432.gp REV ppds24.rev
  FONTS Text/Font/fontIBMPPDSInfo.asm
  PRINTCOM
    ${DOTMATRIX_IBM_COMMON}
    ${DOTMATRIX_NO_COLOR}
    ${DOTMATRIX_IBMX24_GRAPHICS}
    Page/pageStartForceLength.c
    Page/pageEnd.c
    Job/Custom/customPPDS24.c
    Cursor/cursorSetCursorAbs72.c
    Cursor/cursorPrLineFeedSet.c
    Cursor/cursorPrFormFeedIBMPPDS24.c
    Cursor/cursorConvert360.c
    Graphics/graphicsPrintSwath72To360.c
    Graphics/graphicsHi24IntXY.c
    Graphics/Rotate/rotate2pass24.c
    printcomIBMPPDS24Styles.c
  SOURCES ppds24ControlCodes.c)
set(ppds24_GEO ${ppds24_GEO} PARENT_SCOPE)
