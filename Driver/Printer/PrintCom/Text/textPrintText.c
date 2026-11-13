/*
 * textPrintText.c -- PrintText (the C port of textPrintText.asm): the
 * characters through PS_asciiTrans to the printer; a line feed is one of
 * the line spacing, a 0 is left out.  The Japanese build's Kanji are not
 * ported.
 */
#include "printcom.h"

Boolean PrText(PState *ps, const char *text, word count)
{
    word i;
    byte c;

    if (!count)
	while (text[count])
	    count++;
    for (i = 0; i < count; i++) {
	c = ps->PS_asciiTrans[(byte) text[i]];
	if (c == C_LF) {
	    if (SendLineFeed(ps))
		return TRUE;
	} else if (c && PrintStreamWriteByte(ps, c))
	    return TRUE;
    }
    return FALSE;
}

/* PrintText: dx:si (DR_dsSi) = the text, cx = its length (0: up to the
 * null) */
Boolean PrintText(PState *ps, DriverRegs *r)
{
    return PrText(ps, r->DR_dsSi, (word) r->DR_cx);
}
