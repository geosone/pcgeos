/*
 * graphicsTextMetrics.c -- the metrics of fonts and text (after
 * graphicsTextMetrics.asm: GrFontMetrics, GrTextWidth,
 * GrTextWidthWBFixed).
 *
 * A text's width: the widths of its characters (the default character
 * for one that is not there), the space padding on spaces
 * (TM_PAD_SPACES), an optional hyphen only where drawn
 * (TM_DRAW_OPTIONAL_HYPHENS, at the end), and with kerning
 * (TM_TRACK_KERN, TM_PAIR_KERN) the track kerning and the pair's value
 * added to each width that stays positive.
 */
#include "graphicsInt.h"
#include <char.h>

#define TM_KERNING	(TM_TRACK_KERN | TM_PAIR_KERN)

/* the WBFixed of a FontBuf as 1/256 */
static sdword W256(const WBFixed *w)
{
    return ((sdword) w->WBF_int << 8) + w->WBF_frac;
}

/* the value of a GFM_info, rounded or << 8 (WBFixed in dx.ah) */
dword GrFontMetrics(GStateHandle gstate, GFM_info info)
{
    FontBuf *fb = FontDrLockFont(gstate);
    word which = (word) (info & ~1);
    Boolean rounded = (Boolean) (info & 1);
    sdword v;

    if (!fb)
	return 0;
    switch (which) {
    case GFMI_DRIVER:		v = fb->FB_maker; goto plain;
    case GFMI_KERN_COUNT:	v = fb->FB_kernCount; goto plain;
    case GFMI_FIRST_CHAR:	v = fb->FB_firstChar; goto plain;
    case GFMI_LAST_CHAR:	v = fb->FB_lastChar; goto plain;
    case GFMI_DEFAULT_CHAR:	v = fb->FB_defaultChar; goto plain;
    case GFMI_HEIGHT:		v = W256(&fb->FB_height); break;
    case GFMI_MEAN:		v = W256(&fb->FB_mean); break;
    case GFMI_DESCENT:		v = W256(&fb->FB_descent); break;
    case GFMI_BASELINE:		v = W256(&fb->FB_baselinePos); break;
    case GFMI_LEADING:		v = W256(&fb->FB_extLeading); break;
    case GFMI_AVERAGE_WIDTH:	v = W256(&fb->FB_avgwidth); break;
    case GFMI_ASCENT:
	v = W256(&fb->FB_baselinePos) - W256(&fb->FB_accent);
	break;
    case GFMI_MAX_WIDTH:	v = W256(&fb->FB_maxwidth); break;
    case GFMI_MAX_ADJUSTED_HEIGHT:
	v = W256(&fb->FB_height) + W256(&fb->FB_heightAdjust) +
	    W256(&fb->FB_aboveBox) + W256(&fb->FB_belowBox);
	break;
    case GFMI_UNDER_POS:	v = W256(&fb->FB_underPos); break;
    case GFMI_UNDER_THICKNESS:	v = W256(&fb->FB_underThickness); break;
    case GFMI_ABOVE_BOX:	v = W256(&fb->FB_aboveBox); break;
    case GFMI_ACCENT:		v = W256(&fb->FB_accent); break;
    case GFMI_STRIKE_POS:	v = W256(&fb->FB_strikePos); break;
    case GFMI_BELOW_BOX:	v = W256(&fb->FB_belowBox); break;
    default:			v = 0; break;
    }
    FontDrUnlockFont(gstate);
    if (rounded)
	return (dword) (word) ((v + 0x80) >> 8);
    return (dword) (v << 8);
plain:
    FontDrUnlockFont(gstate);
    return rounded ? (dword) (word) v : (dword) v << 16;
}

/* the pair kerning of prev, ch in a font (BBFixed as 1/256) */
sdword GrPairKern(const FontBuf *fb, byte prev, byte ch)
{
    const KernPair *kp = (const KernPair *) ((const byte *) fb +
					     fb->FB_kernPairPtr);
    const byte *kv = (const byte *) fb + fb->FB_kernValuePtr;
    word i;

    for (i = 0; i < fb->FB_kernCount; i++)
	if (kp[i].KP_char1 == ch && kp[i].KP_char2 == prev)
	    return ((sdword) (sbyte) kv[2 * i + 1] << 8) + kv[2 * i];
    return 0;
}

/* the advance of a character in 1/256 (GrTextWidthWBFixed and the text
 * drivers): its width, the padding of a space, the kerning after prev
 * (whose flags are *prevFlags; updated) */
sdword GrCharAdvance(const GState *gs, const FontBuf *fb, byte ch,
		     byte prev, CharTableFlags *prevFlags)
{
    const CharTableEntry *cte = GrCharEntry(fb, ch);
    sdword w = W256(&cte->CTE_width), k;

    if (ch == C_SPACE && (gs->GS_textMode & TM_PAD_SPACES))
	w += ((sdword) gs->GS_textSpacePad.WBF_int << 8) +
	     gs->GS_textSpacePad.WBF_frac;
    if (gs->GS_textMode & TM_KERNING) {
	sdword cw = W256(&cte->CTE_width);
	k = ((sdword) gs->GS_trackKernValue.BBF_int << 8) +
	    gs->GS_trackKernValue.BBF_frac;
	if ((*prevFlags & CTF_IS_FIRST_KERN) &&
	    (cte->CTE_flags & CTF_IS_SECOND_KERN))
	    k += GrPairKern(fb, prev, ch);
	/* the kerned width only when it stays positive */
	w -= cw;
	if (cw + k >= 0)
	    w += cw + k;
	*prevFlags = cte->CTE_flags;
    }
    return w;
}

/* GrTextWidthWBFixed: the width of size characters (0: to the null) */
sdword GrTextWidthLow(const GState *gs, const FontBuf *fb, const char *str,
		      word size)
{
    sdword w = 0;
    byte prev = 0;
    CharTableFlags pf = 0;
    word left = size;

    for (;;) {
	byte ch = (byte) *str++;
	if (!ch)
	    break;
	if (ch == C_OPTHYPHEN) {
	    if ((gs->GS_textMode & TM_DRAW_OPTIONAL_HYPHENS) &&
		(left == 1 || !*str)) {
		ch = C_HYPHEN;
		w += GrCharAdvance(gs, fb, ch, prev, &pf);
	    }
	} else
	    w += GrCharAdvance(gs, fb, ch, prev, &pf);
	prev = ch;
	if (--left == 0)
	    break;
    }
    return w;
}

static sdword TextWidth(GStateHandle gstate, const char *str, word size)
{
    FontBuf *fb = FontDrLockFont(gstate);
    GState *gs;
    sdword w;

    if (!fb)
	return 0;
    gs = MemLock(gstate);
    w = GrTextWidthLow(gs, fb, str, size);
    MemUnlock(gstate);
    FontDrUnlockFont(gstate);
    return w;
}

word GrTextWidth(GStateHandle gstate, const char *str, word size)
{
    return (word) ((TextWidth(gstate, str, size) + 0x80) >> 8);
}

dword GrTextWidthWWFixed(GStateHandle gstate, const char *str, word size)
{
    return (dword) (TextWidth(gstate, str, size) << 8);
}
