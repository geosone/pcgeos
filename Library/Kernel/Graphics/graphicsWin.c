/*
 * graphicsWin.c -- the window of a GState (after graphicsWin.asm:
 * GrInvalRect, GrInvalRectDWord, GrGetWinHandle, GrGetWinBounds,
 * GrGetWinBoundsDWord, GrGetMaskBounds, GrGetMaskBoundsDWord,
 * GetBoundsDWord, MinMaxDWFixed, GrTestRectInMask, LockValidateWindow,
 * DocToWindow, ExtDocToWindowCoords).
 *
 * A rectangle is invalidated in window coordinates; rotated, as the
 * region of its four corners.  The bounds of the window and of its
 * mask go back to document coordinates: the device rectangle less the
 * translation, or, with scaling or rotation, the box round its four
 * corners untransformed.  Without a window the bounds are the whole
 * coordinate space (the clip paths of a graphics string are not kept
 * yet).
 */
#include "graphicsInt.h"
#include "Win/winInt.h"
#include <stdlib.h>

static Boolean FitsWord(sdword v)
{
    return (Boolean) (v >= -32768L && v <= 32767L);
}

/* DocToWindow: rounded device coordinates less the window's position */
static void DocToWindow(const TMatrix *m, const Window *w, sword x, sword y,
			Point *p)
{
    int64_t vx = (int64_t) x * 65536, vy = (int64_t) y * 65536;
    sword rx, ry;

    GrMatTransDWF(m, &vx, &vy);
    if (!GrRoundToWord(vx, &rx) || !GrRoundToWord(vy, &ry))
	rx = ry = (sword) 0x8000;	/* GrTransCoord: overflow */
    p->P_x = (sword) (rx - w->W_winRect.R_left);
    p->P_y = (sword) (ry - w->W_winRect.R_top);
}

/* the region of four window points (a rotated rectangle), without its
 * bounds words; NULL when it is empty */
static word *RotRegion(const Window *w, const Point *p)
{
    sword sep[12];
    word *reg;
    int i;
    Window shifted;

    for (i = 0; i < 4; i++) {
	sep[2 * i] = p[i].P_x;
	sep[2 * i + 1] = p[i].P_y;
    }
    sep[8] = sep[9] = sep[10] = sep[11] = SEPARATOR;
    /* the rows of the window, in window coordinates */
    shifted = *w;
    shifted.W_winRect.R_left = 0;
    shifted.W_winRect.R_top = 0;
    shifted.W_winRect.R_right = (sword) (w->W_winRect.R_right -
					 w->W_winRect.R_left);
    shifted.W_winRect.R_bottom = (sword) (w->W_winRect.R_bottom -
					  w->W_winRect.R_top);
    reg = GrPolygonRegion(&shifted, sep, 4, ODD_EVEN, FALSE);
    return reg;
}

/* invalidate the rectangle or the region (window coordinates) with the
 * window unlocked */
static void Inval(GStateHandle gstate, GState *gs, Window *w, word *reg,
		  sword l, sword t, sword r, sword b)
{
    WindowHandle win = gs->GS_window;
    sword s;

    GrUnlockTransform(gstate, gs, w);
    if (reg) {
	WinInvalReg(win, (const Region *) (reg + 4), 0, 0, 0, 0);
	free(reg);
	return;
    }
    if (l > r) {
	s = l; l = r; r = s;
    }
    if (t > b) {
	s = t; t = b; b = s;
    }
    WinInvalReg(win, NULL, (word) l, (word) t, (word) r, (word) b);
}

void GrInvalRect(GStateHandle gstate, sword left, sword top, sword right,
		 sword bottom)
{
    GState *gs;
    Window *w;
    TMatrix *m = GrLockTransform(gstate, &gs, &w);
    Point p[4];

    if (!w) {
	GrUnlockTransform(gstate, gs, w);
	return;
    }
    DocToWindow(m, w, left, top, &p[0]);
    DocToWindow(m, w, right, bottom, &p[2]);
    if (!(m->TM_flags & TM_ROTATED)) {
	Inval(gstate, gs, w, NULL, p[0].P_x, p[0].P_y, p[2].P_x, p[2].P_y);
	return;
    }
    DocToWindow(m, w, right, top, &p[1]);
    DocToWindow(m, w, left, bottom, &p[3]);
    Inval(gstate, gs, w, RotRegion(w, p), 0, 0, 0, 0);
}

/* ExtDocToWindowCoords, before the window's position: clamped to the
 * coordinate space */
static void ExtDoc(const TMatrix *m, sdword x, sdword y, sdword *px,
		   sdword *py)
{
    int64_t vx = (int64_t) x * 65536, vy = (int64_t) y * 65536;

    GrMatTransDWF(m, &vx, &vy);
    vx = (vx + 0x8000) >> 16;
    vy = (vy + 0x8000) >> 16;
    *px = FitsWord((sdword) vx) && vx == (sdword) vx ? (sdword) vx :
	vx < 0 ? (sdword) (sword) LARGEST_NEGATIVE_COORDINATE :
		 LARGEST_POSITIVE_COORDINATE;
    *py = FitsWord((sdword) vy) && vy == (sdword) vy ? (sdword) vy :
	vy < 0 ? (sdword) (sword) LARGEST_NEGATIVE_COORDINATE :
		 LARGEST_POSITIVE_COORDINATE;
}

void GrInvalRectDWord(GStateHandle gstate, const RectDWord *bounds)
{
    GState *gs;
    Window *w;
    TMatrix *m = GrLockTransform(gstate, &gs, &w);
    sdword x[4], y[4];
    Point p[4];
    int i;

    if (!w) {
	GrUnlockTransform(gstate, gs, w);
	return;
    }
    ExtDoc(m, bounds->RD_left, bounds->RD_top, &x[0], &y[0]);
    ExtDoc(m, bounds->RD_right, bounds->RD_top, &x[1], &y[1]);
    ExtDoc(m, bounds->RD_right, bounds->RD_bottom, &x[2], &y[2]);
    ExtDoc(m, bounds->RD_left, bounds->RD_bottom, &x[3], &y[3]);
    for (i = 0; i < 4; i++) {
	p[i].P_x = (sword) (x[i] - w->W_winRect.R_left);
	p[i].P_y = (sword) (y[i] - w->W_winRect.R_top);
    }
    if (x[3] == x[0] && y[3] == y[2])		/* not rotated */
	Inval(gstate, gs, w, NULL, p[0].P_x, p[0].P_y, p[2].P_x, p[2].P_y);
    else
	Inval(gstate, gs, w, RotRegion(w, p), 0, 0, 0, 0);
}

WindowHandle GrGetWinHandle(GStateHandle gstate)
{
    GState *gs = MemLock(gstate);
    WindowHandle w = gs->GS_window;

    MemUnlock(gstate);
    return w;
}

/* MinMaxDWFixed */
static void MinMax(int64_t x, int64_t y, RectDWord *b)
{
    sdword ix = (sdword) (x >> 16), iy = (sdword) (y >> 16);

    if (ix < b->RD_left)
	b->RD_left = ix;
    if (x & 0xffff)
	ix++;
    if (ix > b->RD_right)
	b->RD_right = ix;
    if (iy < b->RD_top)
	b->RD_top = iy;
    if (y & 0xffff)
	iy++;
    if (iy > b->RD_bottom)
	b->RD_bottom = iy;
}

/*
 * the device rectangle l, t, r, b (right and bottom one past) in
 * document coordinates: less the translation (the asm took one less
 * than its integer part when the fraction was half or more), or the
 * box round the corners untransformed (GetBoundsDWord, which added one
 * to the right and bottom again)
 */
static void DevToDoc(TMatrix *m, sword l, sword t, sword r, sword b,
		     RectDWord *o)
{
    if (!(m->TM_flags & TM_COMPLEX)) {
	sdword tx = (sdword) (GrDWF(&m->TM_31) >> 16);
	sdword ty = (sdword) (GrDWF(&m->TM_32) >> 16);
	if (m->TM_31.DWF_frac & 0x8000)
	    tx--;
	if (m->TM_32.DWF_frac & 0x8000)
	    ty--;
	o->RD_left = l - tx;
	o->RD_right = r - tx;
	o->RD_top = t - ty;
	o->RD_bottom = b - ty;
    } else {
	int64_t xs[4], ys[4];
	int i;

	xs[0] = xs[3] = (int64_t) l * 65536;
	xs[1] = xs[2] = ((int64_t) r + 1) * 65536;
	ys[0] = ys[1] = (int64_t) t * 65536;
	ys[2] = ys[3] = ((int64_t) b + 1) * 65536;
	o->RD_left = o->RD_top = 0x7fffffffL;
	o->RD_right = o->RD_bottom = (sdword) 0x80000000UL;
	for (i = 0; i < 4; i++) {
	    GrMatUntransDWF(m, &xs[i], &ys[i]);
	    MinMax(xs[i], ys[i], o);
	}
    }
}

void GrGetWinBoundsDWord(GStateHandle gstate, RectDWord *bounds)
{
    GState *gs;
    Window *w;
    TMatrix *m = GrLockTransform(gstate, &gs, &w);

    if (!w) {
	bounds->RD_left = bounds->RD_top = (sdword) (sword) MIN_COORD;
	bounds->RD_right = bounds->RD_bottom = MAX_COORD;
    } else if (w->W_regFlags & WRF_CLOSED) {
	bounds->RD_left = bounds->RD_top = (sdword) (sword) MIN_COORD;
	bounds->RD_right = bounds->RD_bottom = (sdword) (sword) MIN_COORD + 1;
    } else
	DevToDoc(m, w->W_winRect.R_left, w->W_winRect.R_top,
		 (sword) (w->W_winRect.R_right + 1),
		 (sword) (w->W_winRect.R_bottom + 1), bounds);
    GrUnlockTransform(gstate, gs, w);
}

static Boolean ToRect(const RectDWord *d, Rectangle *r)
{
    if (!FitsWord(d->RD_left) || !FitsWord(d->RD_top) ||
	!FitsWord(d->RD_right) || !FitsWord(d->RD_bottom))
	return FALSE;
    r->R_left = (sword) d->RD_left;
    r->R_top = (sword) d->RD_top;
    r->R_right = (sword) d->RD_right;
    r->R_bottom = (sword) d->RD_bottom;
    return TRUE;
}

/* TRUE when the bounds fit in words */
Boolean GrGetWinBounds(GStateHandle gstate, Rectangle *bounds)
{
    RectDWord d;

    GrGetWinBoundsDWord(gstate, &d);
    return ToRect(&d, bounds);
}

/* FALSE when the mask is empty */
Boolean GrGetMaskBoundsDWord(GStateHandle gstate, RectDWord *bounds)
{
    GrDraw d;
    Boolean ok = TRUE;

    GrEnterGraphics(gstate, &d);
    if (!d.GD_w) {				/* no clip paths yet */
	bounds->RD_left = bounds->RD_top = (sdword) 0x80000000UL;
	bounds->RD_right = bounds->RD_bottom = 0x7fffffffL;
    } else if (d.GD_w->W_grFlags & WGF_MASK_NULL)
	ok = FALSE;
    else
	DevToDoc(&d.GD_w->W_curTMatrix, d.GD_w->W_maskRect.R_left,
		 d.GD_w->W_maskRect.R_top,
		 (sword) (d.GD_w->W_maskRect.R_right + 1),
		 (sword) (d.GD_w->W_maskRect.R_bottom + 1), bounds);
    GrExitGraphics(&d);
    return ok;
}

/* FALSE when the mask is empty or the bounds do not fit in words; the
 * whole coordinate space without a limit */
Boolean GrGetMaskBounds(GStateHandle gstate, Rectangle *bounds)
{
    RectDWord d;

    if (!GrGetMaskBoundsDWord(gstate, &d))
	return FALSE;
    if (d.RD_left == (sdword) 0x80000000UL) {
	bounds->R_left = bounds->R_top = (sword) MIN_COORD;
	bounds->R_right = bounds->R_bottom = MAX_COORD;
	return TRUE;
    }
    return ToRect(&d, bounds);
}

/* TRRT_IN, TRRT_PARTIAL or TRRT_OUT: the rectangle (document
 * coordinates) against the mask; TRRT_OUT without a window (a
 * graphics string, not kept yet, would be TRRT_IN) */
TestRectReturnType GrTestRectInMask(GStateHandle gstate, sword left,
				    sword top, sword right, sword bottom)
{
    GrDraw d;
    TestRectReturnType r = TRRT_OUT;
    sword l, t, rr, b;

    GrEnterGraphics(gstate, &d);
    if (d.GD_w && !(d.GD_w->W_grFlags & WGF_MASK_NULL) &&
	     GrTransDev(&d, left, top, &l, &t) &&
	     GrTransDev(&d, right, bottom, &rr, &b))
	r = GrTestRectInReg(WinReg(d.GD_win, d.GD_w->W_maskReg), l, t, rr,
			    b);
    GrExitGraphics(&d);
    return r;
}
