/*
 * pageStartASFOnly.c -- PrintStartPage (the C port of
 * pageStartASFOnly.asm): the cursor home and the styles cleared, the
 * form feed or not (the paper comes from a sheet feeder only).
 */
#include "printcom.h"

extern Boolean PrintHomeCursor(PState *ps, DriverRegs *r);

Boolean PrintStartPage(PState *ps, DriverRegs *r)
{
    return PrintHomeCursor(ps, r) || PrSetStyles(ps, 0);
}
