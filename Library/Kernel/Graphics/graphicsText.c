/*
 * graphicsText.c -- drawing text (after graphicsText.asm: GrDrawText,
 * GrDrawTextAtCP, GrDrawChar, GrDrawCharAtCP, TextCallDriver,
 * UpdateTextPos; LibDrawKernelStyles and DrawOneBar of
 * graphicsTextObscure.asm).
 *
 * The position is the text's left edge at its top (the font's box),
 * or at the baseline, the accent or the bottom (TM_DRAW_BASE,
 * TM_DRAW_ACCENT, TM_DRAW_BOTTOM).  It goes through the window's matrix
 * to the video driver (DR_VID_PUTSTRING, WBFixed device coordinates);
 * a bitmap font is drawn unscaled there.  The kernel draws the
 * underline and the strike-through itself (KERNEL_STYLES), a bar from
 * the start to the end of the text in the text attributes.  The pen
 * ends after the text.
 *
 * Not yet: graphics strings and paths (the pen moves, nothing is
 * drawn), the bars of rotated text (a polygon in the asm), right to
 * left text, GrDrawTextField.
 */
#include "graphicsInt.h"

#define TEXT	((word) offsetof(GState, GS_textAttr))

static sdword W256(const WBFixed *w)
{
    return ((sdword) w->WBF_int << 8) + w->WBF_frac;
}

/* DrawOneBar: from x1 to x2 at y (doc, 1/256) pos below, thick high */
static void DrawBar(const GrDraw *d, sdword x1, sdword x2, sdword y,
		    sdword pos, sdword thick)
{
    int64_t ax = (int64_t) x1 << 8, ay = (int64_t) (y + pos) << 8;
    int64_t bx = (int64_t) x2 << 8, by = (int64_t) (y + pos + thick) << 8;
    sword l, t, r, b;

    if (d->GD_w->W_curTMatrix.TM_flags & TM_ROTATED)
	return;
    GrMatTransDWF(&d->GD_w->W_curTMatrix, &ax, &ay);
    GrMatTransDWF(&d->GD_w->W_curTMatrix, &bx, &by);
    if (GrRoundToWord(ax, &l) && GrRoundToWord(ay, &t) &&
	GrRoundToWord(bx, &r) && GrRoundToWord(by, &b))
	GrFillRectLow(d, l, t, r, b, TEXT);
}

/* TextCallDriver: size characters (0: to the null) at x, y (doc,
 * 1/65536) */
static void DrawText(GStateHandle gstate, int64_t x, int64_t y,
		     const char *str, word size)
{
    GrDraw d;
    GState *gs;
    FontBuf *fb;
    const TMatrix *m;
    sdword width, top;
    word n;

    GrEnterGraphics(gstate, &d);
    gs = d.GD_gs;
    m = d.GD_w ? &d.GD_w->W_curTMatrix : &gs->GS_TMatrix;
    fb = GrLockFont(gs, m);
    if (!fb) {
	GrSetDocWWFPenPos(gs, (sdword) x, (sdword) y);
	GrExitGraphics(&d);
	return;
    }
    for (n = 0; (!size || n < size) && str[n]; n++)
	;
    width = GrTextWidthLow(gs, fb, str, n);

    top = (sdword) (y >> 8);
    if (gs->GS_textMode & TM_DRAW_BASE)
	top -= W256(&fb->FB_baselinePos);
    else if (gs->GS_textMode & TM_DRAW_ACCENT)
	top -= W256(&fb->FB_accent);
    else if (gs->GS_textMode & TM_DRAW_BOTTOM)
	top -= W256(&fb->FB_height);

    if (d.GD_w && !(d.GD_w->W_grFlags & WGF_MASK_NULL) && n) {
	int64_t px = x, py = (int64_t) top << 8;
	sword ix, iy;
	GrMatTransDWF(m, &px, &py);
	if (GrRoundToWord(px >> 8 << 8, &ix) &&
	    GrRoundToWord(py >> 8 << 8, &iy)) {
	    VidPutStringArgs a;
	    DriverRegs r;
	    memset(&r, 0, sizeof(r));
	    a.VPS_string = str;
	    a.VPS_numChars = n;
	    a.VPS_font = fb;
	    r.DR_ax = (word) (sword) (px >> 16);
	    r.DR_bx = (word) (sword) (py >> 16);
	    r.DR_dx = (word) ((((py >> 8) & 0xff) << 8) | ((px >> 8) & 0xff));
	    r.DR_bpPtr = &a;
	    GrCallDriver(&d, DR_VID_PUTSTRING, &r, TEXT);
	}
	/* LibDrawKernelStyles */
	if (gs->GS_fontAttr.FCA_textStyle & TS_UNDERLINE)
	    DrawBar(&d, (sdword) (x >> 8), (sdword) (x >> 8) + width, top,
		    W256(&fb->FB_underPos), W256(&fb->FB_underThickness));
	if (gs->GS_fontAttr.FCA_textStyle & TS_STRIKE_THRU)
	    DrawBar(&d, (sdword) (x >> 8), (sdword) (x >> 8) + width, top,
		    W256(&fb->FB_strikePos), W256(&fb->FB_underThickness));
    }
    GrUnlockFont(gs);
    /* UpdateTextPos */
    GrSetDocWWFPenPos(gs, (sdword) (x + ((int64_t) width << 8)), (sdword) y);
    GrExitGraphics(&d);
}

void GrDrawText(GStateHandle gstate, sword x, sword y, const char *str,
		word size)
{
    DrawText(gstate, (int64_t) x * 65536, (int64_t) y * 65536, str, size);
}

void GrDrawTextAtCP(GStateHandle gstate, const char *str, word size)
{
    PointWWFixed cp;

    GrGetCurPosWWFixed(gstate, &cp);
    DrawText(gstate,
	     (int64_t) cp.PF_x.WWF_int * 65536 + cp.PF_x.WWF_frac,
	     (int64_t) cp.PF_y.WWF_int * 65536 + cp.PF_y.WWF_frac, str, size);
}

void GrDrawChar(GStateHandle gstate, sword x, sword y, word ch)
{
    char c = (char) ch;

    DrawText(gstate, (int64_t) x * 65536, (int64_t) y * 65536, &c, 1);
}

void GrDrawCharAtCP(GStateHandle gstate, word ch)
{
    char c = (char) ch;

    GrDrawTextAtCP(gstate, &c, 1);
}
