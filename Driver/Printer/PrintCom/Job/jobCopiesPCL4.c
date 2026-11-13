/*
 * jobCopiesPCL4.c -- the number of copies of the PCL 4 printers (the C
 * port of jobCopiesPCL4.asm): 1 to 99, ESC & l # X.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrintSetCopies(PState *ps, word copies)
{
    if (copies > 99)
	copies = 99;
    if (!copies)
	copies = 1;
    return WriteNumCommand(ps, pr_codes_NumCopies, (byte) copies);
}

/* PrintEscSetCopies: ax = the number of copies */
Boolean PrintEscSetCopies(PState *ps, DriverRegs *r)
{
    return PrintSetCopies(ps, (word) r->DR_ax);
}
