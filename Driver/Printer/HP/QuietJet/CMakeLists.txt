# The HP QuietJet printer driver (quietjet.geo) in C: quietjetManager.asm's
# PrintCom files (the PCL job, page and cursor in 1/192", the dot matrix
# text with the QuietJet's fonts, NLQ, bold and underline, raster lines
# uncompressed at 96 and 192 dpi) and its control codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../DotMatrix/DotMatrix.cmake)
geos_dotmatrix_driver(quietjet GP quietjet32.gp REV quietjet.rev
  INFOS qjetInfo.asm qjetplusInfo.asm
  FONTS Text/Font/fontQuietjetInfo.asm
  PRINTCOM
    printcomEntry.c printcomInfo.c printcomAdmin.c printcomNoEscapes.c
    Job/jobStartPCL.c Job/jobEndPCL4.c Job/jobPaperPCL.c Job/jobPaperInfo.c
    Job/Custom/customPaintJetXL300PCL.c
    printcomPaperOnlyDialog.c
    Page/pageStart.c Page/pageEndASFOnly.c
    Cursor/cursorConvert192.c Cursor/cursorPCLCommon.c
    Cursor/cursorSetCursorPCL.c
    Text/textPrintStyleRun.c Text/textPrintText.c Text/textPrintRaw.c
    Text/textSetFont.c Text/textGetLineSpacing.c
    Text/textSetLineSpacing.c Text/textSetSymbolSet.c
    Text/textLoadNoISOSymbolSet.c
    Styles/stylesGet.c Styles/stylesSet.c Styles/stylesTest.c
    printcomStream.c Stream/streamHexToASCII.c Stream/streamPCLCommand.c
    Buffer/bufferCreate.c Buffer/bufferDestroy.c Buffer/bufferLoadBand.c
    Buffer/bufferScanBand.c Buffer/bufferSendOutput.c
    Buffer/bufferClearOutput.c
    ${DOTMATRIX_NO_COLOR}
    Graphics/graphicsCommon.c Graphics/graphicsPrintSwathPCL.c
    Graphics/graphicsPCLScanline.c
  SOURCES quietjetControlCodes.c quietjetStyles.c)
set(quietjet_GEO ${quietjet_GEO} PARENT_SCOPE)
