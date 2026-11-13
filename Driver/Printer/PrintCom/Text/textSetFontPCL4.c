/*
 * textSetFontPCL4.c -- PrintSetFont of the PCL 4 printers (the C port of
 * textSetFontPCL4.asm): the font kept in PS_curFont; a printer without
 * soft fonts gets the resident font chosen by spacing (the pitch in
 * characters an inch, or proportional), height in points, style, weight
 * and typeface (fontPCLInfo.c).  The soft fonts are chosen with the
 * text (textPrintTextPCL4.c).
 */
#include "printcom.h"
#include "printcomDriver.h"

/* PrintSetFontWayInt */
Boolean PrintSetFontWayInt(PState *ps)
{
    byte pitch = ps->PS_curFont.FE_pitch;
    PrintTextStyle style = ps->PS_asciiStyle;
    int i;

    if (pitch ? WriteNumCommand(ps, pr_codes_SetResidentFixed,
				(byte) (pitch / 10))
	      : WriteNumCommand(ps, pr_codes_SetResidentProportional, 1))
	return TRUE;
    if (HexToAsciiStreamWrite(ps, ps->PS_curFont.FE_size) ||
	PrintStreamWriteByte(ps, 'v') ||
	SendCodeOut(ps, (style & PTS_ITALIC) ? pr_codes_SetResidentItalic
					     : pr_codes_SetResidentUpright) ||
	SendCodeOut(ps, (style & PTS_BOLD) ? pr_codes_SetResidentBold
					   : pr_codes_SetResidentMedium))
	return TRUE;
    for (i = 0; pclFontIDTab[i] != FID_INVALID &&
	     pclFontIDTab[i] != ps->PS_curFont.FE_fontID; i++)
	;
    return HexToAsciiStreamWrite(ps, pclTypefaceTab[i]) ||
	PrintStreamWriteByte(ps, 'T');
}

/* PrintSetFontInt */
Boolean PrSetFont(PState *ps, FontID id, word size, byte pitch)
{
    ps->PS_curFont.FE_pitch = pitch;
    ps->PS_curFont.FE_fontID = id;
    ps->PS_curFont.FE_size = size;
    if (ps->PS_printerSmart != PS_DUMB_RASTER)
	return FALSE;
    return PrintSetFontWayInt(ps);
}

/* PrintSetFont: cx = FontID, dx = size (points), bl = pitch (0:
 * proportional) */
Boolean PrintSetFont(PState *ps, DriverRegs *r)
{
    return PrSetFont(ps, (FontID) r->DR_cx, (word) r->DR_dx,
		     (byte) r->DR_bx);
}
