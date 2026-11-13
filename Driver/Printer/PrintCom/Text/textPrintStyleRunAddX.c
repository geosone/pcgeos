/*
 * textPrintStyleRunAddX.c -- PrintStyleRun (the C port of
 * textPrintStyleRunAddX.asm): as textPrintStyleRun.c, and the cursor's
 * X then the end of the run (the width added), for the printers whose
 * cursor moves by spaces from where the head is
 * (cursorSetCursorSpace72.c).
 *
 * Port32 ABI: DR_dsSi = the StyleRunInfo, DR_esDi = the locked
 * TextStrings block it is in (ds in the asm, whose attribute array
 * TS_textAttributeInfo the run's SRI_attributes indexes), ax and cx =
 * the tile's place on the paper.  The asm compared SRI_attributes with
 * PS_previousAttribute, which it never set; C does the same.  The
 * Japanese build's double-width characters are not ported.
 */
#include "printcom.h"
#include <Internal/spoolInt.h>

Boolean PrintStyleRun(PState *ps, DriverRegs *r)
{
    const StyleRunInfo *sri = r->DR_dsSi;
    TextStrings *ts = r->DR_esDi;
    const TextAttrInfo *tai;
    WWFixedAsDWord w, p;
    word x, y, size, pitch;

    x = (word) (sri->SRI_xPosition + ps->PS_currentMargins.PM_left -
		(word) r->DR_ax);
    y = sri->SRI_yPosition;
    if (!(ps->PS_paperInput & PIO_TRACTOR))
	y = (word) (y - ((word) r->DR_cx - ps->PS_currentMargins.PM_top));
    if (PrSetCursor(ps, (dword) x << 16, (dword) y << 16))
	return TRUE;
    tai = ChunkArrayElementToPtrHandles(ts->TS_header.LMBH_handle,
					ts->TS_textAttributeInfo,
					sri->SRI_attributes, &size);
    if (sri->SRI_attributes != ps->PS_previousAttribute) {
	ps->PS_cursorPos.P_x = (sword) (ps->PS_cursorPos.P_x +
					sri->SRI_stringWidth.WBF_int);
	/* the average width of a character, the pitch it makes (720 /
	 * width, rounded, at most 255) */
	w = GrUDivWWFixed(((dword) sri->SRI_stringWidth.WBF_int << 16) |
			  ((word) sri->SRI_stringWidth.WBF_frac << 8),
			  (dword) sri->SRI_numChars << 16);
	p = GrUDivWWFixed(720UL << 16, w);
	pitch = (word) ((p >> 16) + ((p >> 15) & 1));
	if (pitch > 255)
	    pitch = 255;
	if (PrSetFont(ps, tai->TAI_font, tai->TAI_size.WBF_int, (byte) pitch) ||
	    PrSetColorRGB(ps, tai->TAI_color.RGB_red, tai->TAI_color.RGB_green,
			  tai->TAI_color.RGB_blue))
	    return TRUE;
    }
    if (PrSetStyles(ps, PrTestStyles(ps, tai->TAI_style)))
	return TRUE;
    return PrText(ps, (const char *) &sri->SRI_text, sri->SRI_numChars);
}
