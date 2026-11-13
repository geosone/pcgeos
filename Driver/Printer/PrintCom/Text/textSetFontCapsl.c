/*
 * textSetFontCapsl.c -- PrintSetFont (the C port of textSetFontCapsl.asm):
 * a scalable font of a CaPSL printer: the entry of the font ID in the
 * mode's list (NLQ outside the text modes; the first if none matches)
 * with the size and pitch asked for, its typeface code, the size in dots
 * (CSI n SP C) and the pitch (CSI ? n 0 SP K, n the TextPitch).
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrSetFont(PState *ps, FontID id, word size, byte pitch)
{
    const PrinterInfo *pi = ps->PS_deviceInfo;
    const word *list;
    const FontEntry *fe;
    word mode, i;

    mode = ps->PS_mode >= PM_FIRST_TEXT_MODE ? ps->PS_mode : PM_TEXT_NLQ;
    i = *(const word *) ((const byte *) &pi->PI_lowRes + mode);
    if (!i)
	return FALSE;
    list = (const word *) (printFontInfoBase + i);
    for (i = 0; list[i]; i++)
	if (((const FontEntry *) (printFontInfoBase + list[i]))->FE_fontID ==
	    id)
	    break;
    fe = (const FontEntry *) (printFontInfoBase + list[list[i] ? i : 0]);
    ps->PS_curFont = *fe;
    ps->PS_curFont.FE_size = size;
    ps->PS_curFont.FE_pitch = pitch;
    return SendCodeOut(ps, printFontCommands[ps->PS_curFont.FE_command]) ||
	SendCodeOut(ps, pr_codes_SetSizeStart) ||
	HexToAsciiStreamWrite(ps, PrConvertToDriverCoordinates(
	    (WWFixedAsDWord) ps->PS_curFont.FE_size << 16)) ||
	SendCodeOut(ps, pr_codes_SetSizeEnd) ||
	SendCodeOut(ps, pr_codes_SetPitchStart) ||
	HexToAsciiStreamWrite(ps, ps->PS_curFont.FE_pitch) ||
	SendCodeOut(ps, pr_codes_SetPitchEnd);
}

/* PrintSetFont: cx = FontID, dx = size (points), bl = pitch (0:
 * proportional) */
Boolean PrintSetFont(PState *ps, DriverRegs *r)
{
    return PrSetFont(ps, (FontID) r->DR_cx, (word) r->DR_dx,
		     (byte) r->DR_bx);
}
