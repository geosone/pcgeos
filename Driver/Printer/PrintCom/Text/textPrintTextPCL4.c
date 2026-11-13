/*
 * textPrintTextPCL4.c -- PrintText of the PCL 4 printers (the C port of
 * textPrintTextPCL4.asm).  A printer of soft fonts gets the face of the
 * current font (an outline font; else URW Roman 12, and the printer's
 * 10 pitch font as URW Mono 12) and the characters of the text
 * downloaded; spaces are moves by their width (FontSendSpace).  A printer
 * without soft fonts gets the characters through the translation table.
 */
#include "printcom.h"

Boolean PrTextPCL4(PState *ps, const byte *text, word count)
{
    Boolean soft = (Boolean) (ps->PS_printerSmart != PS_DUMB_RASTER);
    word i;
    byte c;

    if (soft) {
	if (ps->PS_curFont.FE_fontID == FID_PRINTER_10CPI)
	    PrintSetURWMono12(ps);
	if (GrCheckFontAvailID(FEF_OUTLINES, 0, ps->PS_curFont.FE_fontID) ==
	    FID_INVALID) {
	    ps->PS_curFont.FE_fontID = FID_DTC_URW_ROMAN;
	    ps->PS_curFont.FE_size = 12;
	}
	FontAddFace(ps);
    }
    if (!count)
	while (text[count])
	    count++;
    if (!count)
	return FALSE;
    if (soft && FontAddChars(ps, text, count))
	return TRUE;
    for (i = 0; i < count; i++) {
	c = text[i];
	if (!soft)
	    c = ps->PS_asciiTrans[c];
	if (c == C_LF) {
	    if (SendLineFeed(ps))
		return TRUE;
	} else if (c == C_SPACE || c == C_NONBRKSPACE) {
	    if (FontSendSpace(ps))
		return TRUE;
	} else if (c > C_SPACE && PrintStreamWriteByte(ps, c))
	    return TRUE;
    }
    return FALSE;
}

Boolean PrText(PState *ps, const char *text, word count)
{
    return PrTextPCL4(ps, (const byte *) text, count);
}

/* PrintText: dx:si (DR_dsSi) = the text, cx = its length (0: up to the
 * null) */
Boolean PrintText(PState *ps, DriverRegs *r)
{
    return PrTextPCL4(ps, r->DR_dsSi, (word) r->DR_cx);
}
