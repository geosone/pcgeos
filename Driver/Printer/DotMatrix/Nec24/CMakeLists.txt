# The Late Model NEC 24-pin printer driver (nec24.geo) in C:
# nec24Manager.asm's PrintCom files (the LQ2 cursor and graphics, 360 x 360
# dpi in two interleaves) and its control codes.
geos_dotmatrix_driver(nec24 GP nec2432.gp REV nec24.rev
  FONTS Text/Font/fontNECInfo.asm
  PRINTCOM
    ${DOTMATRIX_LQ2_COMMON}
    ${DOTMATRIX_LQ2_GRAPHICS}
  SOURCES nec24ControlCodes.c)
