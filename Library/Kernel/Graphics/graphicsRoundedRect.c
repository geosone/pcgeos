/*
 * graphicsRoundedRect.c -- rectangles with rounded corners (after
 * graphicsRoundedRect.asm: GrDrawRoundRect, GrDrawRoundRectTo,
 * GrFillRoundRect, GrFillRoundRectTo, PrepRoundRectLow, DoRoundCorner,
 * CombineCorners, AddPointCheckBehind, DrawRoundRectLow,
 * FillRoundRectLow).
 *
 * The radius is at most half the width (unsigned) and half the height.
 * Each corner is a quarter circle (a bounded arc of CalcEllipse, from 0
 * to 90 degrees at the top right, then counterclockwise; in reverse
 * when the matrix mirrors), and the four are joined, with their ends,
 * into one closed polyline.  It is drawn in the line attributes or
 * filled as a convex polygon.  The "to" forms go from the pen, which
 * stays; the others leave the pen at the first corner.  Not yet:
 * graphics strings and paths.
 */
#include "graphicsInt.h"
#include <stdlib.h>

#define AREA	((word) offsetof(GState, GS_areaAttr))

typedef struct {
    sword	*pts;
    word	count;
    GrArcParams	ap;
} Corner;

/* AddPointCheckBehind */
static void Add(sword *o, word *n, sword x, sword y)
{
    if (*n && o[2 * *n - 2] == x && o[2 * *n - 1] == y)
	return;
    o[2 * *n] = x;
    o[2 * *n + 1] = y;
    (*n)++;
}

/* PrepRoundRectLow: the outline, *count points (the first again at the
 * end), in the C heap */
static sword *PrepRoundRect(const GrDraw *d, sword l, sword t, sword r,
			    sword b, word radius, word *count)
{
    static const sword angles[5] = { 0, 90, 180, 270, 0 };
    Corner c[4];
    sword s, cx[4], cy[4], top, bot, *o;
    word hw, hh, n = 0, total = 10;
    int i, j, step;

    if (l > r) {
	s = l; l = r; r = s;
    }
    if (t > b) {
	s = t; t = b; b = s;
    }
    hw = (word) ((word) (r - l) >> 1);
    if (hw < radius)
	radius = hw;
    hh = (word) ((word) (b - t) >> 1);
    if ((sword) hh < (sword) radius)
	radius = hh;
    s = (sword) radius;
    l = (sword) (l + s); t = (sword) (t + s);
    r = (sword) (r - s); b = (sword) (b - s);
    cx[0] = r; cy[0] = t;
    cx[1] = l; cy[1] = t;
    cx[2] = l; cy[2] = b;
    cx[3] = r; cy[3] = b;

    for (i = 0; i < 4; i++) {			/* DoRoundCorner */
	sword bl = (sword) (cx[i] - s), bt = (sword) (cy[i] - s);
	sword br = (sword) (cx[i] + s), bb = (sword) (cy[i] + s);

	memset(&c[i].ap, 0, sizeof(c[i].ap));
	GrSetupArcLow(d, bl, bt, br, bb, angles[i], angles[i + 1],
		      &c[i].ap);
	c[i].ap.GA_start = c[i].ap.GA_startPoint;
	c[i].ap.GA_end = c[i].ap.GA_endPoint;
	c[i].count = 0;
	c[i].pts = GrCalcEllipse(d, bl, bt, br, bb, GR_CET_BOUNDED_ARC_RR,
				 &c[i].ap, &c[i].count, &top, &bot);
	if (!c[i].pts)
	    c[i].count = 0;
	total = (word) (total + c[i].count);
    }

    /* CombineCorners */
    o = malloc((size_t) total * 2 * sizeof(sword));
    if (o) {
	step = GrTestFlip(d) ? -1 : 1;
	j = step > 0 ? 0 : 3;
	for (i = 0; i < 4; i++, j += step) {
	    const Corner *k = &c[j];
	    Add(o, &n, k->ap.GA_start.P_x, k->ap.GA_start.P_y);
	    if (k->count) {
		Add(o, &n, k->pts[0], k->pts[1]);
		memcpy(&o[2 * n], &k->pts[2],
		       (size_t) (k->count - 1) * 2 * sizeof(sword));
		n = (word) (n + k->count - 1);
	    }
	    Add(o, &n, k->ap.GA_end.P_x, k->ap.GA_end.P_y);
	}
	j = step > 0 ? 0 : 3;
	Add(o, &n, c[j].ap.GA_start.P_x, c[j].ap.GA_start.P_y);
	o[2 * n] = o[2 * n + 1] = (sword) EOREGREC;
    }
    for (i = 0; i < 4; i++)
	free(c[i].pts);
    *count = n;
    return o;
}

static void RoundRect(GrDraw *d, sword l, sword t, sword r, sword b,
		      word radius, Boolean fill)
{
    sword *pts;
    word count;
    DriverRegs regs;

    if (GrTrivialReject(d))
	return;
    pts = PrepRoundRect(d, l, t, r, b, radius, &count);
    if (!pts)
	return;
    if (!fill)					/* DrawRoundRectLow */
	GrDrawPolylineLow(d, pts, count, TRUE);
    else if (count - 1 > 2) {			/* FillRoundRectLow */
	memset(&regs, 0, sizeof(regs));
	regs.DR_cx = (word) (count - 1);
	regs.DR_farPtr = pts;
	GrCallDriver(d, DR_VID_POLYGON, &regs, AREA);
    }
    free(pts);
}

void GrDrawRoundRect(GStateHandle gstate, sword left, sword top, sword right,
		     sword bottom, word radius)
{
    GrDraw d;

    GrEnterGraphics(gstate, &d);
    GrSetDocPenPos(d.GD_gs, left, top);
    RoundRect(&d, left, top, right, bottom, radius, FALSE);
    GrExitGraphics(&d);
}

void GrFillRoundRect(GStateHandle gstate, sword left, sword top, sword right,
		     sword bottom, word radius)
{
    GrDraw d;

    GrEnterGraphics(gstate, &d);
    GrSetDocPenPos(d.GD_gs, left, top);
    RoundRect(&d, left, top, right, bottom, radius, TRUE);
    GrExitGraphics(&d);
}

/* the C stub took the right edge first, whatever graphics.h names it */
static void RoundRectTo(GStateHandle gstate, sword right, sword bottom,
			word radius, Boolean fill)
{
    GrDraw d;
    XYValueAsDWord pen = GrGetCurPos(gstate);	/* GetDocPenPos */

    GrEnterGraphics(gstate, &d);
    RoundRect(&d, (sword) (word) pen, (sword) (word) (pen >> 16), right,
	      bottom, radius, fill);
    GrExitGraphics(&d);
}

void GrDrawRoundRectTo(GStateHandle gstate, sword bottom, sword right,
		       word radius)
{
    RoundRectTo(gstate, bottom, right, radius, FALSE);
}

void GrFillRoundRectTo(GStateHandle gstate, sword right, sword bottom,
		       word radius)
{
    RoundRectTo(gstate, right, bottom, radius, TRUE);
}
