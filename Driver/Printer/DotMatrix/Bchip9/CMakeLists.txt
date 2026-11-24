# The BlueChip 9-pin printer driver (bchip9.geo) in C: bchip9Manager.asm's
# PrintCom files (MX cursor by tabs, MX graphics, its country codes) and its
# control codes.
geos_dotmatrix_driver(bchip9 GP bchip932.gp REV bchip9.rev
  FONTS Text/Font/fontEpsonMXInfo.asm
  INFOS bchip9generInfo.asm
  PRINTCOM
    ${DOTMATRIX_BASE}
    ${DOTMATRIX_EPSON_JOB}
    ${DOTMATRIX_NO_COLOR}
    ${DOTMATRIX_9PIN_CURSOR}
    ${DOTMATRIX_MX_GRAPHICS}
    printcomCountryDialog.c
    Cursor/cursorSetCursorTab72.c
    Cursor/cursorConvert216.c
    Text/textPrintStyleRun.c
    Text/textLoadBchipSymbolSet.c
    printcomEpsonStyles.c
  SOURCES bchip9ControlCodes.c)
