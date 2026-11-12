/*
 * vidcomLine.c -- DR_VID_LINE, DR_VID_POLYLINE, DR_VID_DASH_LINE and
 * DR_VID_DASH_FILL (after vidcomLine.asm: VidDrawLine, VidLineDDA,
 * VidPolyline, VidDashLine, VidDashFill, InitDashPattern,
 * NextPatternRun).
 *
 * All go through VidLineRuns: Bresenham's line from point 1 to point 2,
 * both ends on, in runs: the pixels of a horizontal (x major) or
 * vertical (y major) stretch.  A run is a rectangle through the
 * window's mask (VidFillClipped), so a line costs a rectangle a run.
 * The asm stepped its own DDA; where the error ties, a pixel may
 * differ.
 *
 * POLYLINE draws connected lines with a brush (ah high, al wide, at
 * least 1x1) and the line attributes; the points (bx:si) are in the
 * separator format: SEPARATOR between polylines, two at the end.  The
 * first pixel of each line after the first is left out, as the one
 * before drew it.
 *
 * The dashed lines (bx:dx a DashInfo; no pattern: solid) step the
 * pattern of on/off lengths (WWFixed pixels) along the major axis from
 * DI_pattIdx, the pixel where the line before stopped, and leave it
 * where this one stopped.  DASH_LINE with al 1 leaves out the first
 * pixel (not counted either).  DASH_FILL fills each dash and its copy
 * moved by ax, cx: the polygon of the four.
 */
#include "vidcom.h"

void VidLineRuns(sword x1, sword y1, sword x2, sword y2, Boolean skipFirst,
		 VidRunFn *fn, void *ctx)
{
    sword dx, dy, sx, sy, run;
    long err;
    word k = 0;
    Boolean skip = skipFirst;

    dx = (sword) (x2 > x1 ? x2 - x1 : x1 - x2);
    dy = (sword) (y2 > y1 ? y2 - y1 : y1 - y2);
    sx = (sword) (x2 >= x1 ? 1 : -1);
    sy = (sword) (y2 >= y1 ? 1 : -1);
    if (dx >= dy) {			/* x major: horizontal runs */
	err = 2L * dy - dx;
	run = x1;
	for (;;) {
	    if (x1 == x2 || err > 0) {
		if (skip) {
		    skip = FALSE;
		    run = (sword) (run + sx);
		}
		if ((sx > 0 && run <= x1) || (sx < 0 && run >= x1)) {
		    fn(run, y1, x1, y1, k, ctx);
		    k = (word) (k + (sx > 0 ? x1 - run : run - x1) + 1);
		}
		if (x1 == x2)
		    break;
		y1 = (sword) (y1 + sy);
		err -= 2L * dx;
		run = (sword) (x1 + sx);
	    }
	    err += 2L * dy;
	    x1 = (sword) (x1 + sx);
	}
    } else {				/* y major: vertical runs */
	err = 2L * dx - dy;
	run = y1;
	for (;;) {
	    if (y1 == y2 || err > 0) {
		if (skip) {
		    skip = FALSE;
		    run = (sword) (run + sy);
		}
		if ((sy > 0 && run <= y1) || (sy < 0 && run >= y1)) {
		    fn(x1, run, x1, y1, k, ctx);
		    k = (word) (k + (sy > 0 ? y1 - run : run - y1) + 1);
		}
		if (y1 == y2)
		    break;
		x1 = (sword) (x1 + sx);
		err -= 2L * dy;
		run = (sword) (y1 + sy);
	    }
	    err += 2L * dx;
	    y1 = (sword) (y1 + sy);
	}
    }
}

/* a run with a brush bw x bh (1x1: the run) */
typedef struct {
    Window		*w;
    const VidDrawState	*ds;
    sword		bw, bh;
} RunCtx;

static void FillRun(sword x1, sword y1, sword x2, sword y2, word k,
		    void *ctx)
{
    const RunCtx *c = ctx;

    (void) k;
    VidFillClipped(c->w, x1 < x2 ? x1 : x2, y1 < y2 ? y1 : y2,
		   (sword) ((x1 < x2 ? x2 : x1) + c->bw - 1),
		   (sword) ((y1 < y2 ? y2 : y1) + c->bh - 1), c->ds);
}

static const CommonAttr *Attr(DriverRegs *r)
{
    return (const CommonAttr *) ((byte *) r->DR_dsSi + (word) r->DR_si);
}

static void Line(Window *w, const VidDrawState *ds, sword x1, sword y1,
		 sword x2, sword y2, Boolean skipFirst)
{
    RunCtx c;

    c.w = w;
    c.ds = ds;
    c.bw = c.bh = 1;
    VidDevCondHidePtr(x1 < x2 ? x1 : x2, y1 < y2 ? y1 : y2,
		      x1 < x2 ? x2 : x1, y1 < y2 ? y2 : y1);
    VidLineRuns(x1, y1, x2, y2, skipFirst, FillRun, &c);
    VidDevCondShowPtr();
}

void VidDrawLine(DriverRegs *r)
{
    Window *w = r->DR_esDi;
    VidDrawState ds;

    if (!r->DR_dsSi || !w)
	return;
    VidSetupDraw(r->DR_dsSi, Attr(r), w, &ds);
    Line(w, &ds, (sword) (word) r->DR_ax, (sword) (word) r->DR_bx,
	 (sword) (word) r->DR_cx, (sword) (word) r->DR_dx, FALSE);
}

/* the bounds of a polyline buffer, with its brush */
Boolean VidPolylineBounds(const sword *p, sword bw, sword bh, Rectangle *b)
{
    Boolean any = FALSE;

    b->R_left = b->R_top = 0x7fff;
    b->R_right = b->R_bottom = (sword) 0x8000;
    for (;;) {
	if (p[0] == SEPARATOR) {
	    if (p[1] == SEPARATOR)
		break;
	    p++;
	    continue;
	}
	any = TRUE;
	if (p[0] < b->R_left)
	    b->R_left = p[0];
	if (p[0] > b->R_right)
	    b->R_right = p[0];
	if (p[1] < b->R_top)
	    b->R_top = p[1];
	if (p[1] > b->R_bottom)
	    b->R_bottom = p[1];
	p += 2;
    }
    b->R_right = (sword) (b->R_right + bw - 1);
    b->R_bottom = (sword) (b->R_bottom + bh - 1);
    return any;
}

void VidPolyline(DriverRegs *r)
{
    GState *gs = r->DR_dsSi;
    Window *w = r->DR_esDi;
    const sword *p = r->DR_farPtr;
    VidDrawState ds;
    RunCtx c;
    Rectangle b;
    sword x, y;
    Boolean first;

    if (!gs || !w || !p)
	return;
    VidSetupDraw(gs, &gs->GS_lineAttr, w, &ds);
    c.w = w;
    c.ds = &ds;
    c.bw = (sword) (byte) r->DR_ax;
    c.bh = (sword) (byte) (r->DR_ax >> 8);
    if (c.bw < 1)
	c.bw = 1;
    if (c.bh < 1)
	c.bh = 1;
    if (!VidPolylineBounds(p, c.bw, c.bh, &b))
	return;
    VidDevCondHidePtr(b.R_left, b.R_top, b.R_right, b.R_bottom);
    if (p[0] == SEPARATOR)
	p++;
    x = p[0];
    y = p[1];
    p += 2;
    first = TRUE;
    for (;;) {
	if (p[0] == SEPARATOR) {
	    if (p[1] == SEPARATOR)
		break;
	    x = p[1];			/* a new polyline */
	    y = p[2];
	    p += 3;
	    first = TRUE;
	    continue;
	}
	VidLineRuns(x, y, p[0], p[1], !first, FillRun, &c);
	first = FALSE;
	x = p[0];
	y = p[1];
	p += 2;
    }
    VidDevCondShowPtr();
}

/*
 * Dashes.
 */
typedef struct {
    Window		*w;
    const VidDrawState	*ds;
    const DashInfo	*di;
    dword		total;		/* the pattern's length, 16.16 */
    word		start;		/* DI_pattIdx */
    /* DASH_FILL */
    Boolean		fill;
    sword		ddx, ddy;
    Boolean		inDash;
    Point		dashStart, dashEnd;
} DashCtx;

static dword WW(const WWFixed *f)
{
    return ((dword) (word) f->WWF_int << 16) | f->WWF_frac;
}

/* pixel n of the line: on the pattern? */
static Boolean DashOn(const DashCtx *c, word n)
{
    dword pos = ((dword) (word) (c->start + n) << 16) % c->total;
    word i;

    for (i = 0; i < 2 * c->di->DI_nPairs; i++) {
	dword len = WW(&c->di->DI_patt[i]);
	if (pos < len)
	    return (i & 1) == 0;
	pos -= len;
    }
    return FALSE;
}

static void EndDash(DashCtx *c)
{
    Point q[4];

    if (!c->inDash)
	return;
    c->inDash = FALSE;
    q[0] = c->dashStart;
    q[1] = c->dashEnd;
    q[2].P_x = (sword) (c->dashEnd.P_x + c->ddx);
    q[2].P_y = (sword) (c->dashEnd.P_y + c->ddy);
    q[3].P_x = (sword) (c->dashStart.P_x + c->ddx);
    q[3].P_y = (sword) (c->dashStart.P_y + c->ddy);
    VidFillPolygon(c->w, c->ds, q, 4);
}

/* a dash of a run, its ends in any order */
static void FillSpan(const DashCtx *c, sword x1, sword y1, sword x2,
		     sword y2)
{
    VidFillClipped(c->w, x1 < x2 ? x1 : x2, y1 < y2 ? y1 : y2,
		   x1 < x2 ? x2 : x1, y1 < y2 ? y2 : y1, c->ds);
}

static void DashRun(sword x1, sword y1, sword x2, sword y2, word k,
		    void *ctx)
{
    DashCtx *c = ctx;
    sword sx = (sword) (x2 > x1 ? 1 : x2 < x1 ? -1 : 0);
    sword sy = (sword) (y2 > y1 ? 1 : y2 < y1 ? -1 : 0);
    sword x = x1, y = y1, ax = 0, ay = 0;
    Boolean on, prev = FALSE;

    for (;;) {
	on = DashOn(c, k);
	if (c->fill) {
	    if (on) {
		if (!c->inDash) {
		    c->inDash = TRUE;
		    c->dashStart.P_x = x;
		    c->dashStart.P_y = y;
		}
		c->dashEnd.P_x = x;
		c->dashEnd.P_y = y;
	    } else
		EndDash(c);
	} else {
	    if (on && !prev) {
		ax = x;
		ay = y;
	    }
	    if (!on && prev)
		FillSpan(c, ax, ay, (sword) (x - sx), (sword) (y - sy));
	    prev = on;
	}
	if (x == x2 && y == y2)
	    break;
	x = (sword) (x + sx);
	y = (sword) (y + sy);
	k++;
    }
    if (!c->fill && prev)
	FillSpan(c, ax, ay, x, y);
}

static word LinePixels(const DashInfo *di, Boolean skip)
{
    sword dx = (sword) (di->DI_pt2.P_x - di->DI_pt1.P_x);
    sword dy = (sword) (di->DI_pt2.P_y - di->DI_pt1.P_y);

    if (dx < 0)
	dx = (sword) -dx;
    if (dy < 0)
	dy = (sword) -dy;
    return (word) ((dx > dy ? dx : dy) + (skip ? 0 : 1));
}

static void Dash(DriverRegs *r, Boolean fill)
{
    Window *w = r->DR_esDi;
    DashInfo *di = r->DR_farPtr;
    VidDrawState ds;
    DashCtx c;
    Boolean skip = !fill && (byte) r->DR_ax == 1;
    word i, size;
    sword l, t, rt, b;

    if (!r->DR_dsSi || !w || !di)
	return;
    VidSetupDraw(r->DR_dsSi, Attr(r), w, &ds);
    memset(&c, 0, sizeof(c));
    c.w = w;
    c.ds = &ds;
    c.di = di;
    for (i = 0; di->DI_patt && i < 2 * di->DI_nPairs; i++)
	c.total += WW(&di->DI_patt[i]);
    if (!fill && !c.total) {		/* solid */
	Line(w, &ds, di->DI_pt1.P_x, di->DI_pt1.P_y, di->DI_pt2.P_x,
	     di->DI_pt2.P_y, skip);
	return;
    }
    if (fill) {
	c.fill = TRUE;
	c.ddx = (sword) (word) r->DR_ax;
	c.ddy = (sword) (word) r->DR_cx;
    }
    size = (word) (c.total >> 16);
    c.start = size ? (word) (di->DI_pattIdx % size) : 0;
    l = di->DI_pt1.P_x < di->DI_pt2.P_x ? di->DI_pt1.P_x : di->DI_pt2.P_x;
    rt = di->DI_pt1.P_x < di->DI_pt2.P_x ? di->DI_pt2.P_x : di->DI_pt1.P_x;
    t = di->DI_pt1.P_y < di->DI_pt2.P_y ? di->DI_pt1.P_y : di->DI_pt2.P_y;
    b = di->DI_pt1.P_y < di->DI_pt2.P_y ? di->DI_pt2.P_y : di->DI_pt1.P_y;
    if (fill) {
	if (c.ddx < 0) l = (sword) (l + c.ddx); else rt = (sword) (rt + c.ddx);
	if (c.ddy < 0) t = (sword) (t + c.ddy); else b = (sword) (b + c.ddy);
    }
    VidDevCondHidePtr(l, t, rt, b);
    if (!c.total)			/* solid and fat: one dash */
	c.total = 0x10000, c.di = NULL;
    if (c.di)
	VidLineRuns(di->DI_pt1.P_x, di->DI_pt1.P_y, di->DI_pt2.P_x,
		    di->DI_pt2.P_y, skip, DashRun, &c);
    else {
	Point q[4];
	q[0] = di->DI_pt1;
	q[1] = di->DI_pt2;
	q[2].P_x = (sword) (di->DI_pt2.P_x + c.ddx);
	q[2].P_y = (sword) (di->DI_pt2.P_y + c.ddy);
	q[3].P_x = (sword) (di->DI_pt1.P_x + c.ddx);
	q[3].P_y = (sword) (di->DI_pt1.P_y + c.ddy);
	VidFillPolygon(w, &ds, q, 4);
    }
    EndDash(&c);
    VidDevCondShowPtr();
    if (size)
	di->DI_pattIdx = (word) ((c.start + LinePixels(di, skip)) % size);
}

void VidDashLine(DriverRegs *r)
{
    Dash(r, FALSE);
}

void VidDashFill(DriverRegs *r)
{
    Dash(r, TRUE);
}
