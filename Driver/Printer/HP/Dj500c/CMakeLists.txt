# The HP DeskJet CMY printer driver (dj500c.geo) in C: dj500cManager.asm's
# PrintCom files (the PCL job with the PaintJet XL300's PJL entry and exit,
# page, cursor and stream, no text and no styles, the CMY raster planes a scan
# line at a time, TIFF compressed) and its control codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../DotMatrix/DotMatrix.cmake)
geos_dotmatrix_driver(dj500c GP dj500c32.gp REV dj500c.rev
  INFOS dj500cInfo.asm pjxl300Info.asm nccCMYInfo.asm
  PRINTCOM
    printcomEntry.c printcomInfo.c printcomAdmin.c printcomNoEscapes.c
    Job/jobStartPCL.c Job/jobEndPCL4.c Job/jobPaperPCL.c Job/jobPaperInfo.c
    Job/Custom/customPaintJetXL300PCL.c
    printcomDeskjetDialog.c
    Page/pageStart.c Page/pageEndASFOnly.c
    Cursor/cursorConvert300.c Cursor/cursorPCLCommon.c
    Cursor/cursorSetCursorPCL.c Cursor/cursor1ScanlineFeed.c
    printcomNoText.c printcomNoStyles.c
    printcomStream.c Stream/streamHexToASCII.c Stream/streamPCLCommand.c
    Buffer/bufferPCLCreate.c Buffer/bufferDestroy.c
    Color/colorSetNextMono.c Color/colorSetFirstMono.c
    Color/colorGetFormat.c Color/colorSetNone.c
    Graphics/graphicsCommon.c Graphics/graphicsPrintSwathPCLCMY.c
    Graphics/graphicsPCLTIFF.c
  SOURCES dj500cControlCodes.c)
set(dj500c_GEO ${dj500c_GEO} PARENT_SCOPE)
