/*
 * jobCopiesCapsl.c -- PrintSetCopies and PrintEscSetCopies (the C port of
 * jobCopiesCapsl.asm): CSI n v, 1 to 99 copies.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrintSetCopies(PState *ps, word copies)
{
    if (copies > 99)
	copies = 99;
    if (!copies)
	copies = 1;
    return SendCodeOut(ps, pr_codes_CSIcode) ||
	HexToAsciiStreamWrite(ps, copies) ||
	PrintStreamWriteByte(ps, 'v');
}

/* PrintEscSetCopies: ax = the number of copies */
Boolean PrintEscSetCopies(PState *ps, DriverRegs *r)
{
    return PrintSetCopies(ps, (word) r->DR_ax);
}
