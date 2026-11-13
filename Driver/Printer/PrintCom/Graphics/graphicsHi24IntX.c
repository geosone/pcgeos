/*
 * graphicsHi24IntX.c -- PrPrintHighBand (the C port of
 * graphicsHi24IntX.asm): a band of 24 scan lines in two passes, the even
 * columns, then the odd ones (360 x 180 dpi on the Epson 24-pin).
 */
#include "printcom.h"

Boolean PrPrintHighBand(PState *ps)
{
    word start = ps->PS_newScanNumber;

    return PrSend24HiresLines(ps, &start);
}
