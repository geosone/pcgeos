/*
 * printcomNoText.c -- the text routines of a driver without text modes
 * (the C port of printcomNoText.asm and Text/textPrintRaw.asm).
 */
#include "printcom.h"

/* PrintStyleRun, PrintText, PrintSetFont, PrintGetLineSpacing and
 * PrintSetLineSpacing: nothing to do */
static Boolean NoText(PState *ps, DriverRegs *r)
{
    (void) ps;
    (void) r;
    return FALSE;
}

Boolean PrintStyleRun(PState *ps, DriverRegs *r)	{ return NoText(ps, r); }
Boolean PrintText(PState *ps, DriverRegs *r)		{ return NoText(ps, r); }
Boolean PrintSetFont(PState *ps, DriverRegs *r)		{ return NoText(ps, r); }
Boolean PrintGetLineSpacing(PState *ps, DriverRegs *r)	{ return NoText(ps, r); }
Boolean PrintSetLineSpacing(PState *ps, DriverRegs *r)	{ return NoText(ps, r); }

/* PrintRaw: dx:si (DR_dsSi) = buffer, cx = count */
Boolean PrintRaw(PState *ps, DriverRegs *r)
{
    return PrintStreamWrite(ps, r->DR_dsSi, (word) r->DR_cx);
}

/* PrintSetSymbolSet, PrintLoadSymbolSet: no symbol set without text */
Boolean PrintSetSymbolSet(PState *ps)
{
    (void) ps;
    return FALSE;
}

Boolean PrintLoadSymbolSet(PState *ps)
{
    (void) ps;
    return FALSE;
}
