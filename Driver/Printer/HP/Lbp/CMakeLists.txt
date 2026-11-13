# The Canon CaPSL laser printer driver (lbp.geo) in C: lbpManager.asm's
# PrintCom files (the CaPSL job with copies, paper sizes and trays, the
# cursor in 1/300" absolute, the scalable fonts with their size and pitch,
# seven styles, the bands as raster images of their live bytes at 75, 150
# and 300 dpi) and its control codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../DotMatrix/DotMatrix.cmake)
geos_dotmatrix_driver(lbp GP lbp32.gp REV lbp.rev
  INFOS capsl2Info.asm capsl3Info.asm
  FONTS Text/Font/fontCapslInfo.asm
  PRINTCOM
    printcomEntry.c printcomInfo.c printcomAdmin.c printcomNoEscapes.c
    Job/jobStartCapsl.c Job/jobCopiesCapsl.c Job/jobEndDummy.c
    Job/jobPaperCapsl.c Job/jobPaperInfo.c
    printcomPaperOnlyDialog.c
    Stream/streamHexToASCII.c printcomStream.c
    Buffer/bufferCreate.c Buffer/bufferDestroy.c Buffer/bufferLoadBand.c
    Buffer/bufferScanBand.c Buffer/bufferSendOutput.c
    Buffer/bufferClearOutput.c
    Text/textPrintStyleRun.c Text/textPrintText.c Text/textPrintRaw.c
    Text/textSetFontCapsl.c Text/textGetLineSpacing.c
    Text/textSetLineSpacing.c Text/textSetSymbolSet.c
    Text/textLoadNoISOSymbolSet.c
    Styles/stylesGet.c Styles/stylesSet.c Styles/stylesTest.c
    Graphics/graphicsCommon.c Graphics/graphicsCapslCommon.c
    Graphics/graphicsAdjustForResolution.c Graphics/graphicsPrintSwath300.c
    Cursor/cursorConvert300.c Cursor/cursorPCLCommon.c
    Cursor/cursorSetCursorCapsl.c
    Page/pageStartASFOnly.c Page/pageEndASFOnly.c
    ${DOTMATRIX_NO_COLOR}
  SOURCES lbpControlCodes.c lbpStyles.c)
set(lbp_GEO ${lbp_GEO} PARENT_SCOPE)
