/*
 * textPrintStyleRunPCL4.c -- PrintStyleRun of the PCL 4 printers (the C
 * port of textPrintStyleRunPCL4.asm): the cursor to the run; on a new
 * attribute the font (proportional unless it is a monospaced outline
 * font: then the pitch from the average width of a character, 720 /
 * width rounded, at most 255), its optional attributes for the soft
 * fonts (color, space padding, weight, width, track kerning) and the
 * styles; then the characters.
 */
#include "printcom.h"
#include <Internal/spoolInt.h>

Boolean PrintStyleRun(PState *ps, DriverRegs *r)
{
    const StyleRunInfo *sri = r->DR_dsSi;
    TextStrings *ts = r->DR_esDi;
    const TextAttrInfo *tai;
    WWFixedAsDWord w, p;
    word x, y, size, pitch = 0;

    x = (word) (sri->SRI_xPosition + ps->PS_currentMargins.PM_left -
		(word) r->DR_ax);
    y = sri->SRI_yPosition;
    if (!(ps->PS_paperInput & PIO_TRACTOR))
	y = (word) (y - ((word) r->DR_cx - ps->PS_currentMargins.PM_top));
    if (PrSetCursor(ps, (dword) x << 16, (dword) y << 16))
	return TRUE;
    if (sri->SRI_attributes != ps->PS_previousAttribute) {
	ps->PS_previousAttribute = sri->SRI_attributes;
	tai = ChunkArrayElementToPtrHandles(ts->TS_header.LMBH_handle,
					    ts->TS_textAttributeInfo,
					    sri->SRI_attributes, &size);
	if (GrCheckFontAvailID(FEF_FAMILY | FEF_OUTLINES, FF_MONO,
			       tai->TAI_font) != FID_INVALID) {
	    w = GrUDivWWFixed(((dword) sri->SRI_stringWidth.WBF_int << 16) |
			      ((word) sri->SRI_stringWidth.WBF_frac << 8),
			      (dword) sri->SRI_numChars << 16);
	    p = GrUDivWWFixed(720UL << 16, w);
	    pitch = (word) ((p >> 16) + ((p >> 15) & 1));
	    if (pitch > 255)
		pitch = 255;
	}
	ps->PS_curOptFont.OFE_color = tai->TAI_color;
	ps->PS_curOptFont.OFE_spacePad = tai->TAI_spacePad.WBF_int;
	ps->PS_curOptFont.OFE_fontWeight = tai->TAI_fontWeight;
	ps->PS_curOptFont.OFE_fontWidth = tai->TAI_fontWidth;
	ps->PS_curOptFont.OFE_trackKern = tai->TAI_trackKern;
	ps->PS_asciiStyle = (PrintTextStyle) tai->TAI_style;
	if (PrSetFont(ps, tai->TAI_font, tai->TAI_size.WBF_int, (byte) pitch) ||
	    PrintSetStylesInt(ps))
	    return TRUE;
    }
    return PrText(ps, (const char *) &sri->SRI_text, sri->SRI_numChars);
}
