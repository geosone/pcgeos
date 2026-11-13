/*
 * textSetFontPCL.c -- PrintSetFont of the PCL drivers (the C port of
 * textSetFontPCL.asm): the printer's resident fonts chosen by spacing
 * (the pitch in characters an inch, or proportional), height in points,
 * style (upright or italic), weight (medium or bold) and typeface (by
 * the font ID, fontPCLInfo.c); underline on or off.  The font and its
 * styles are the PState's, so the styles are not set apart.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrSetFont(PState *ps, FontID id, word size, byte pitch)
{
    return PrSetFontStyle(ps, id, size, pitch, ps->PS_asciiStyle);
}

Boolean PrSetFontStyle(PState *ps, FontID id, word size, byte pitch,
		       PrintTextStyle style)
{
    int i;

    ps->PS_curFont.FE_pitch = pitch;
    ps->PS_curFont.FE_fontID = id;
    ps->PS_curFont.FE_size = size;
    ps->PS_asciiStyle = style;
    if (ps->PS_printerSmart != PS_DUMB_RASTER)
	return FALSE;
    if (pitch ? WriteNumCommand(ps, pr_codes_SetResidentFixed,
				(word) (pitch / 10))
	      : WriteNumCommand(ps, pr_codes_SetResidentProportional, 1))
	return TRUE;
    if (HexToAsciiStreamWrite(ps, size) || PrintStreamWriteByte(ps, 'v') ||
	SendCodeOut(ps, (style & PTS_ITALIC) ? pr_codes_SetResidentItalic
					     : pr_codes_SetResidentUpright) ||
	SendCodeOut(ps, (style & PTS_BOLD) ? pr_codes_SetResidentBold
					   : pr_codes_SetResidentMedium))
	return TRUE;
    for (i = 0; pclFontIDTab[i] != FID_INVALID && pclFontIDTab[i] != id; i++)
	;
    if (HexToAsciiStreamWrite(ps, pclTypefaceTab[i]) ||
	PrintStreamWriteByte(ps, 'T'))
	return TRUE;
    return SendCodeOut(ps, (style & PTS_UNDERLINE) ? pr_codes_SetUnderline
						   : pr_codes_ResetUnderline);
}

/* PrintSetFont: cx = FontID, dx = size (points), bl = pitch (0:
 * proportional), si = the PrintTextStyle */
Boolean PrintSetFont(PState *ps, DriverRegs *r)
{
    return PrSetFontStyle(ps, (FontID) r->DR_cx, (word) r->DR_dx,
			  (byte) r->DR_bx, (PrintTextStyle) r->DR_si);
}
