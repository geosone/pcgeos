# The Epson 24-pin printer driver (epson24.geo) in C: epson24Manager.asm's
# PrintCom files (LQ cursor, text and graphics) and its control codes.
geos_dotmatrix_driver(epson24 GP epson2432.gp REV epson24.rev
  FONTS Text/Font/fontEpsonLQInfo.asm
  PRINTCOM ${DOTMATRIX_EPSON_COMMON}
    Cursor/cursorPrFormFeed60.c Cursor/cursorConvert180.c
    Graphics/graphicsPrintSwath180.c Graphics/graphicsHi24IntX.c
    Graphics/Rotate/rotate2pass24.c Graphics/graphicsMed24.c
    Graphics/Rotate/rotate24.c
  SOURCES epson24ControlCodes.c)
