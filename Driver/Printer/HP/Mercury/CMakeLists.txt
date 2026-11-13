# The HP DeskJet KCMY printer driver (mercury.geo) in C: mercuryManager.asm's
# PrintCom files (the PCL job with the PaintJet XL300's PJL entry and exit,
# page, cursor and stream, no text and no styles, the KCMY raster planes a
# scan line at a time, TIFF compressed) and its control codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../DotMatrix/DotMatrix.cmake)
geos_dotmatrix_driver(mercury GP mercury32.gp REV mercury.rev
  INFOS mercuryInfo.asm
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
    Graphics/graphicsCommon.c Graphics/graphicsPrintSwathPCLKCMY.c
    Graphics/graphicsPCLTIFF.c
  SOURCES mercuryControlCodes.c)
set(mercury_GEO ${mercury_GEO} PARENT_SCOPE)
