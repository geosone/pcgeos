/*
 * fontPCLInfo.c -- the PCL typefaces of the GEOS fonts (the C port of
 * fontPCLInfo.asm): pclTypefaceTab[i] for pclFontIDTab[i], the last
 * (Line Printer) for any other font.
 */
#include "printcom.h"

const FontID pclFontIDTab[] = {
    FID_DTC_URW_ROMAN, FID_DTC_URW_SANS, FID_DTC_URW_MONO,
    FID_DTC_CENTURY_SCHOOLBOOK, FID_DTC_BROADWAY, FID_DTC_COOPER_C_BLACK,
    FID_DTC_FRANKLIN_GOTHIC, FID_DTC_UNIVERSITY_ROMAN, FID_DTC_PRESTIGE_ELITE,
    FID_DTC_GARAMOND, FID_DTC_OPTIMA, FID_DTC_BODONI, FID_INVALID
};
const word pclTypefaceTab[] = {
    5,		/* Times-Roman */
    4,		/* Helv */
    3,		/* Courier */
    23,		/* Century Schoolbook */
    21,		/* Broadway */
    19,		/* Cooper Black */
    6,		/* Letter Gothic (for Franklin Gothic) */
    24,		/* University Roman */
    8,		/* Prestige Elite */
    18,		/* Garamond */
    17,		/* Optima */
    22,		/* Bodoni */
    0		/* Line Printer */
};
