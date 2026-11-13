# The HP DeskJet printer driver (deskjet.geo) in C: deskjetManager.asm's
# PrintCom files (the PCL job, page, cursor, text, styles, stream and
# buffer, the TIFF compressed raster) and its control codes.
include(${CMAKE_CURRENT_SOURCE_DIR}/../../DotMatrix/DotMatrix.cmake)
geos_dotmatrix_driver(deskjet GP deskjet32.gp REV deskjet.rev
  INFOS deskjetdjInfo.asm
  PRINTCOM
    printcomEntry.c printcomInfo.c printcomAdmin.c printcomNoEscapes.c
    Job/jobStartPCL.c Job/jobEndPCL4.c Job/jobPaperPCL.c Job/jobPaperInfo.c
    printcomDeskjetDialog.c
    Page/pageStart.c Page/pageEndASFOnly.c
    Cursor/cursorConvert300.c Cursor/cursorPCLCommon.c
    Cursor/cursorSetCursorPCL.c
    Text/textPrintStyleRunPCL.c Text/textPrintText.c Text/textPrintRaw.c
    Text/textSetFontPCL.c Text/textGetLineSpacing.c
    Text/textSetLineSpacing.c Text/textSetSymbolSet.c
    Text/textLoadNoISOSymbolSet.c Text/Font/fontPCLInfo.c
    printcomPCLStyles.c Styles/stylesGet.c Styles/stylesSet.c
    Styles/stylesTest.c
    printcomStream.c Stream/streamHexToASCII.c Stream/streamPCLCommand.c
    Buffer/bufferPCLCreate.c Buffer/bufferDestroy.c
    Color/colorSetNextMono.c Color/colorSetFirstMono.c
    Color/colorGetFormat.c Color/colorSetNone.c
    Graphics/graphicsCommon.c Graphics/graphicsPrintSwathPCL.c
    Graphics/graphicsPCLTIFF.c
  SOURCES deskjetControlCodes.c)
set(deskjet_GEO ${deskjet_GEO} PARENT_SCOPE)
