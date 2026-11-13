/*
 * textPrintRaw.c -- PrintRaw (the C port of textPrintRaw.asm): dx:si
 * (DR_dsSi) = the bytes, cx = their count, to the printer as they are.
 */
#include "printcom.h"

Boolean PrintRaw(PState *ps, DriverRegs *r)
{
    return PrintStreamWrite(ps, r->DR_dsSi, (word) r->DR_cx);
}
