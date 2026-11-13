# The Canon BJC printer driver (canonbjc.geo) in C: canonBJCManager.asm's
# PrintCom files (the job of the raster mode, a sheet feeder only, raster
# skips, no text and no styles, the planes of a scan line PackBits
# compressed, 180 and 360 dpi) and its control codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../DotMatrix.cmake)
geos_dotmatrix_driver(canonbjc GP canonbjc32.gp REV canonbjc.rev
  PREFIX canonBJC
  INFOS canonBJCmonoInfo.asm canonBJCcmyInfo.asm canonBJCcmykInfo.asm
  PRINTCOM
    printcomEntry.c printcomInfo.c printcomAdmin.c printcomNoEscapes.c
    Job/jobStartCanonBJC.c Job/jobEndCanonBJC.c
    Job/jobPaperPathNoASFControl.c Job/jobPaperInfo.c
    Job/jobResetPrinterAndWait.c
    Page/pageStartASFOnly.c Page/pageEndASFOnly.c
    Cursor/cursorDotMatrixCommon.c Cursor/cursorSetCursorAbsCanonBJC.c
    Cursor/cursorPrLineFeedCanonBJC.c Cursor/cursorPrFormFeedGuess.c
    Cursor/cursorConvert360.c
    printcomStream.c
    Color/colorGetFormat.c Color/colorSetNone.c
    Graphics/graphicsCommon.c Graphics/graphicsCanonBJC.c
    printcomPaperOnlyDialog.c
    printcomNoStyles.c printcomNoText.c
    Buffer/bufferCreate.c Buffer/bufferDestroy.c
  SOURCES canonBJCControlCodes.c)
set(canonbjc_GEO ${canonbjc_GEO} PARENT_SCOPE)
