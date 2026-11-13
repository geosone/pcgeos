# The HP PCL 4 printer driver (pcl4.geo, the LaserJets) in C:
# pcl4Manager.asm's PrintCom files (the PCL job with the copies, duplex
# and the devices' entry and exit codes, page, cursor, styles, the
# graphics as one raster or as runs, the text in soft fonts GEOS draws
# and downloads) and its control codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../DotMatrix/DotMatrix.cmake)
geos_dotmatrix_driver(pcl4 GP pcl432.gp REV pcl4.rev
  INFOS laserjet2Info.asm laserjet2CompInfo.asm downloadInfo.asm
    ppdsInfo.asm internalInfo.asm downloadDuplexInfo.asm ibm4019Info.asm
    totalResetInfo.asm laserjet3SiInfo.asm laserjet4Info.asm
    internalDuplexInfo.asm ibm4039Info.asm paintjetXL300Info.asm
  PRINTCOM
    printcomEntry.c printcomInfo.c printcomAdmin.c
    Graphics/graphicsCommon.c printcomStream.c
    Stream/streamHexToASCII.c Stream/streamPCLCommand.c
    Buffer/bufferPCLCreate.c Buffer/bufferDestroy.c
    Job/jobStartPCL.c Job/jobEndPCL4.c Job/jobPaperPCL.c Job/jobPaperInfo.c
    Job/jobCopiesPCL4.c Job/Custom/customPPDSToPCL.c
    Job/Custom/customPJLToPCL.c Job/Custom/customLJ4PCL.c
    Job/Custom/customIBMPJLToPCL.c Job/Custom/customTotalResetPCL.c
    Styles/stylesGetPCL4.c Styles/stylesSetPCL4.c Styles/stylesTestPCL4.c
    Graphics/graphicsPrintSwathPCL4.c Graphics/graphicsSendBitmapPCL4.c
    Graphics/graphicsSendBitmapCompressedPCL4.c
    Graphics/graphicsAdjustForResolution.c
    Cursor/cursorConvert300.c Cursor/cursorPCLCommon.c
    Cursor/cursorSetCursorPCL.c
    Page/pageStartPCL4.c Page/pageEndPCL4.c
    Text/textGetLineSpacing.c Text/textSetLineSpacing.c
    Text/textPrintTextPCL4.c Text/textPrintRaw.c Text/textSetFontPCL4.c
    Text/textInitFontPCL4.c Text/textSetSymbolSet.c
    Text/textLoadNoISOSymbolSet.c Text/textPrintStyleRunPCL4.c
    Text/Font/fontTopLevelPCL4.c Text/Font/fontDownloadPCL4.c
    Text/Font/fontInternalPCL4.c Text/Font/fontUtilsPCL4.c
    Text/Font/fontPCLInfo.c
    printcomPCL4Dialog.c
    Color/colorSetNextMono.c Color/colorSetFirstMono.c
    Color/colorGetFormat.c Color/colorSetNone.c
  SOURCES pcl4ControlCodes.c pcl4Tables.c)
set(pcl4_GEO ${pcl4_GEO} PARENT_SCOPE)
