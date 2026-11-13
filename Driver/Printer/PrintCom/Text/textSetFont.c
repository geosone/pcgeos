/*
 * textSetFont.c -- PrintSetFont (the C port of textSetFont.asm): the
 * printer font closest to the one asked for (SpoolMapToPrinterFont), its
 * FontEntry from the mode's list in printerFontInfo (else the list's
 * first), the symbol set, the font's code and its mandatory styles.
 */
#include "printcom.h"

Boolean PrSetFont(PState *ps, FontID id, word size, byte pitch)
{
    const PrinterInfo *pi = ps->PS_deviceInfo;
    const word *list;
    const FontEntry *fe = NULL;
    PrinterSymbolSet oldSet;
    word mode, i;

    if (PrintClearStyles(ps))
	return TRUE;
    mode = ps->PS_mode >= PM_FIRST_TEXT_MODE ? ps->PS_mode : PM_TEXT_NLQ;
    SpoolMapToPrinterFont(pi, &id, &size, &pitch);
    /* PI_draft, PI_nlq: the mode's list in printerFontInfo */
    i = *(const word *) ((const byte *) &pi->PI_lowRes + mode);
    if (!i)
	return FALSE;
    list = (const word *) (printFontInfoBase + i);
    for (i = 0; list[i]; i++) {
	fe = (const FontEntry *) (printFontInfoBase + list[i]);
	if (fe->FE_fontID == id && fe->FE_pitch == pitch &&
	    fe->FE_size == size)
	    break;
    }
    if (!list[i])
	fe = (const FontEntry *) (printFontInfoBase + list[0]);
    oldSet = ps->PS_curFont.FE_symbolSet;
    ps->PS_curFont = *fe;
    PrintSetSymbolSet(ps);
    if (oldSet != ps->PS_curFont.FE_symbolSet && PrintLoadSymbolSet(ps))
	return TRUE;
    if (SendCodeOut(ps, printFontCommands[ps->PS_curFont.FE_command]))
	return TRUE;
    return PrSetStyles(ps, PrTestStyles(ps, 0));
}

/* PrintSetFont: cx = FontID, dx = size (points), bl = pitch (0:
 * proportional) */
Boolean PrintSetFont(PState *ps, DriverRegs *r)
{
    return PrSetFont(ps, (FontID) r->DR_cx, (word) r->DR_dx,
		     (byte) r->DR_bx);
}
