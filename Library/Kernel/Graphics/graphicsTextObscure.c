/*
 * graphicsTextObscure.c -- the bounds of a text (after
 * graphicsTextObscure.asm: GrGetTextBounds, CheckCharBox,
 * QuickCharMetrics, CharBoundX, CharBoundY, CalcReturnValues).
 *
 * The text is laid out as GrTextWidthWBFixed does (graphicsTextMetrics.c:
 * widths, space padding, kerning, optional hyphens) from the position on
 * the baseline (the text mode says where the position is); each
 * character's box comes from the font driver (DR_FONT_CHAR_METRICS) and
 * widens the bounds.  A bitmap or printer font has no metrics: its
 * bounds are the box of the font's height and the text's width.  The
 * bounds are rounded away from the text (1.1 to 2, -1.1 to -2) in
 * document coordinates, as the asm.
 *
 * Port32: the starting bounds (the position plus 0x1000 on the low
 * side) do not wrap at 16 bits.
 */
#include "graphicsInt.h"
#include <Internal/fontDr.h>
#include <char.h>

static sdword W256(const WBFixed *w)
{
    return ((sdword) w->WBF_int << 8) + w->WBF_frac;
}

/* QuickCharMetrics: a metric (not rounded) in 1/256; FALSE without a
 * font driver */
static Boolean Metric(GStateHandle gstate, FontMaker maker, word ch,
		      GCM_info info, sdword *v)
{
    DriverRegs r;
    GState *gs = MemLock(gstate);
    Boolean err;

    memset(&r, 0, sizeof(r));
    r.DR_dx = ch;
    r.DR_cx = info;
    r.DR_esDi = gs;
    err = GrCallFontDriverID(maker, DR_FONT_CHAR_METRICS, &r);
    MemUnlock(gstate);
    if (err)
	return FALSE;
    *v = (sdword) (((dword) (word) r.DR_dx << 8) |
		   (((word) r.DR_ax >> 8) & 0xff));
    if (r.DR_dx & 0x8000)
	*v |= (sdword) 0xff000000UL;
    return TRUE;
}

typedef struct {
    sdword	left, top, right, bottom;	/* 1/256 */
} Bounds;

/* CharBoundX, CharBoundY: a new low bound may be a new high one too */
static void BoundX(Bounds *b, sdword v)
{
    if (v < b->left)
	b->left = v;
    if (v > b->right)
	b->right = v;
}

static void BoundY(Bounds *b, sdword v)
{
    if (v < b->top)
	b->top = v;
    if (v > b->bottom)
	b->bottom = v;
}

/* CheckCharBox: the box of ch at (x, y) (baseline); FALSE without a font
 * driver */
static Boolean CharBox(GStateHandle gstate, FontMaker maker, word ch,
		       sdword x, sdword y, Bounds *b)
{
    sdword v;

    if (!Metric(gstate, maker, ch, GCMI_MIN_X, &v))
	return FALSE;
    BoundX(b, v + x);
    if (!Metric(gstate, maker, ch, GCMI_MAX_X, &v))
	return FALSE;
    BoundX(b, v + x);
    if (!Metric(gstate, maker, ch, GCMI_MIN_Y, &v))
	return FALSE;
    BoundY(b, y - v);
    if (!Metric(gstate, maker, ch, GCMI_MAX_Y, &v))
	return FALSE;
    BoundY(b, y - v);
    return TRUE;
}

/* GetRoundedValue: a fraction moves a positive value up; a negative one
 * is its integer (the floor) */
static sword Rounded(sdword v)
{
    sword i = (sword) (v >> 8);

    if (i >= 0 && (v & 0xff))
	i++;
    return i;
}

/* GrGetTextBounds: the bounds of count characters (0: to the null) of
 * str drawn at (xpos, ypos); TRUE (bounds not set) if the font has no
 * driver to give the characters' metrics */
Boolean GrGetTextBounds(GStateHandle gstate, const char *str, word xpos,
			word ypos, word count, Rectangle *bounds)
{
    FontBuf *fb = FontDrLockFont(gstate);
    GState *gs;
    TextMode tm;
    FontMaker maker;
    sdword x = (sdword) (sword) xpos << 8, y = (sdword) (sword) ypos << 8;
    Bounds b;
    byte prev = 0;
    CharTableFlags pf = 0;
    word left = count;

    if (!fb)
	return TRUE;
    gs = MemLock(gstate);
    tm = gs->GS_textMode;
    b.left = x + (0x1000L << 8);
    b.right = x;
    b.top = y + (0x1000L << 8);
    b.bottom = y;
    /* to the baseline */
    if (!(tm & TM_DRAW_BASE)) {
	if (tm & TM_DRAW_BOTTOM)
	    y -= W256(&fb->FB_descent);
	else {
	    y += W256(&fb->FB_baselinePos);
	    if (tm & TM_DRAW_ACCENT)
		y -= W256(&fb->FB_accent);
	}
    }
    maker = fb->FB_maker;
    if (maker == FM_BITMAP || maker == FM_PRINTER) {
	/* computeBMbounds */
	y -= W256(&fb->FB_baselinePos);
	b.left = b.right = x;
	b.top = b.bottom = y;
	b.bottom += W256(&fb->FB_height);
	b.right += GrTextWidthLow(gs, fb, str, count);
	MemUnlock(gstate);
	FontDrUnlockFont(gstate);
    } else {
	MemUnlock(gstate);
	FontDrUnlockFont(gstate);
	for (;;) {
	    byte ch = (byte) *str++;
	    if (!ch)
		break;
	    if (!CharBox(gstate, maker, ch, x, y, &b))
		return TRUE;
	    if (ch != C_OPTHYPHEN ||
		((tm & TM_DRAW_OPTIONAL_HYPHENS) && (left == 1 || !*str))) {
		if (ch == C_OPTHYPHEN)
		    ch = C_HYPHEN;
		fb = FontDrLockFont(gstate);
		if (fb) {
		    gs = MemLock(gstate);
		    x += GrCharAdvance(gs, fb, ch, prev, &pf);
		    MemUnlock(gstate);
		    FontDrUnlockFont(gstate);
		}
	    }
	    prev = ch;
	    if (--left == 0)
		break;
	}
    }
    bounds->R_left = Rounded(b.left);
    bounds->R_top = Rounded(b.top);
    bounds->R_right = Rounded(b.right);
    bounds->R_bottom = Rounded(b.bottom);
    return FALSE;
}

void GrSetTrackKern(GStateHandle gstate, word tk)
{
    GState *gs = MemLock(gstate);

    (void) GrGSRecord(gstate, GR_SET_TRACK_KERN, "w", tk);
    if ((sword) tk != gs->GS_trackKernDegree) {
	gs->GS_trackKernDegree = (sword) tk;
	GrRecalcKern(gs);
    }
    MemUnlock(gstate);
}

word GrGetTrackKern(GStateHandle gstate)
{
    GState *gs = MemLock(gstate);
    word tk = (word) gs->GS_trackKernDegree;

    MemUnlock(gstate);
    return tk;
}
