# The Epson RX 9-pin printer driver (eprx9.geo) in C: eprx9Manager.asm's
# PrintCom files (RX cursor by spaces, RX text, 240 x 216 dpi graphics) and
# its control codes.
geos_dotmatrix_driver(eprx9 GP eprx932.gp REV eprx9.rev
  FONTS Text/Font/fontEpsonMXInfo.asm
  PRINTCOM
    ${DOTMATRIX_BASE}
    ${DOTMATRIX_EPSON_JOB}
    ${DOTMATRIX_NO_COLOR}
    ${DOTMATRIX_9PIN_CURSOR}
    ${DOTMATRIX_FX_GRAPHICS}
    printcomCountryDialog.c
    Cursor/cursorSetCursorSpace72.c
    Cursor/cursorConvert216.c
    Text/textPrintStyleRunAddX.c
    Text/textLoadEpsonSymbolSet.c
    printcomEpsonStyles.c
  SOURCES eprx9ControlCodes.c)
