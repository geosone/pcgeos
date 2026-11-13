/*
 * fontUtilsPCL4.c -- the soft font manager's conversions (the C port of
 * fontUtilsPCL4.asm): points to 300 dpi dots (times 4.1666) and quarter
 * dots, rounded; the styles the font driver draws.
 */
#include "fontPCL4.h"

#define HP_SCALE	((4L << 16) | 0x2aaa)	/* 300 / 72 */

/* DoHPScale: v256 (dx.ah) times the scale, WWFixed */
static sdword DoHPScale(sdword v256)
{
    return (sdword) GrMulWWFixed((WWFixedAsDWord) (v256 * 256), HP_SCALE);
}

sword PointsToPCL(sdword v256)
{
    return (sword) ((DoHPScale(v256) + 0x8000L) >> 16);
}

sword PointsToPCL4(sdword v256)
{
    return (sword) ((DoHPScale(v256) * 4 + 0x8000L) >> 16);
}

/* ConvertTextStyles: italic and bold to the HP style and weight too;
 * underline is a style of the printer (stylesSetPCL4.c) */
TextStyle ConvertTextStyles(PrintTextStyle style, byte *hpStyle,
			    sbyte *hpWeight)
{
    TextStyle ts = 0;

    *hpStyle = 0;			/* HPFS_UPRIGHT */
    *hpWeight = 0;			/* HPSW_BOOK */
    if (style & PTS_ITALIC) {
	*hpStyle = 1;			/* HPFS_ITALIC */
	ts |= TS_ITALIC;
    }
    if (style & PTS_BOLD) {
	*hpWeight = 3;			/* HPSW_BOLD */
	ts |= TS_BOLD;
    }
    if (style & PTS_SUBSCRIPT)
	ts |= TS_SUBSCRIPT;
    else if (style & PTS_SUPERSCRIPT)
	ts |= TS_SUPERSCRIPT;
    if (style & PTS_STRIKETHRU)
	ts |= TS_STRIKE_THRU;
    return ts;
}
