/*
 * textPrintStyleRunPCL.c -- PrintStyleRun of the PCL drivers (the C port
 * of textPrintStyleRunPCL.asm): the cursor to the run, on a new attribute
 * the font with its styles (PrSetFontStyle: the pitch from the average
 * width of a character, 720 / width rounded, at most 255), then the
 * characters.
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
    if (sri->SRI_attributes != ps->PS_previousAttribute) {
	tai = ChunkArrayElementToPtrHandles(ts->TS_header.LMBH_handle,
					    ts->TS_textAttributeInfo,
					    sri->SRI_attributes, &size);
	w = GrUDivWWFixed(((dword) sri->SRI_stringWidth.WBF_int << 16) |
			  ((word) sri->SRI_stringWidth.WBF_frac << 8),
			  (dword) sri->SRI_numChars << 16);
	p = GrUDivWWFixed(720UL << 16, w);
	pitch = (word) ((p >> 16) + ((p >> 15) & 1));
	if (pitch > 255)
	    pitch = 255;
	/* the color: none (printcomNoColor) */
	if (PrSetFontStyle(ps, tai->TAI_font, tai->TAI_size.WBF_int,
			   (byte) pitch, (PrintTextStyle) tai->TAI_style))
	    return TRUE;
    }
    return PrText(ps, (const char *) &sri->SRI_text, sri->SRI_numChars);
}
