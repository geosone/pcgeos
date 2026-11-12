/*
 * graphicsArcLow.c -- drawing and filling ellipses and arcs (after
 * graphicsArcLow.asm: DrawArcEllipseLow, FillArcEllipseLow,
 * SetupEllipseLow, SetupEllipseLineLow, SetupArcLow, SetupArc3PointLow,
 * ArcGetAnglePos, CloseArc, AddPointAfterCheck, TempFillPolygonLow,
 * GetEllipsePointWithAng, CalcEllipsePointWithAng, NormalizeAngle,
 * Order3PointsInArc, Convert3PointToBounds, AveragePointAndStore,
 * CalcInverseSlopeAndStore, CalcLineLineIntersection,
 * CalcPointPointDistance, SqrWWFixed, MulWWFixed, SqrRootDWFixed).
 *
 * The points come from CalcEllipse (graphicsCalcEllipse.c).  An outline
 * is a polyline in the line attributes (thin, fat or dashed like any
 * polyline), closed for an ellipse.  A filled ellipse is convex and goes
 * to the driver as DR_VID_POLYGON.
 *
 * An arc is closed by CloseArc: a chord goes from its end back to its
 * start, a pie through the center.  A filled arc is a polygon by the
 * odd-even rule (a region, graphicsPolygon.c).
 *
 * A bounded arc's ends are the points of the ellipse at the two angles
 * (degrees counterclockwise from 3 o'clock, document coordinates, then
 * transformed).  A 3-point arc goes through its three points: the
 * center is where the perpendicular bisectors of 1-2 and 2-3 meet, and
 * the bounds are the circle round it through point 3.  Points 1 and 3
 * are swapped if needed so the arc runs counterclockwise through 2.
 * Three points on a line make a line from 1 to 3 (the asm drew it
 * untransformed; it is transformed here).
 */
#include "graphicsInt.h"
#include <stdlib.h>

#define LINE	((word) offsetof(GState, GS_lineAttr))
#define INFINITE_SLOPE	0x7fffffffL

static sdword WWF(const WWFixed *w)
{
    return (sdword) (((dword) (word) w->WWF_int << 16) | w->WWF_frac);
}

static void SetWWF(WWFixed *w, sdword v)
{
    w->WWF_int = (sword) (v >> 16);
    w->WWF_frac = (word) v;
}

static sword Round(sdword v)
{
    return (sword) ((v + 0x8000L) >> 16);
}

/* CloseArc: the end, the center for a pie, the start and the first point
 * after the arc's points; TRUE when closed */
Boolean GrCloseArc(sword **pts, word *count, const GrArcParams *ap)
{
    sword *p = realloc(*pts, ((size_t) *count + 6) * 2 * sizeof(sword));
    word n = *count;

    if (!p)
	return FALSE;
    *pts = p;
    if (ap->GA_close != ACT_OPEN) {
	/* AddPointAfterCheck: not if the same as the last (the first) */
	if (!n || p[2 * n - 2] != ap->GA_end.P_x ||
	    p[2 * n - 1] != ap->GA_end.P_y) {
	    p[2 * n] = ap->GA_end.P_x;
	    p[2 * n + 1] = ap->GA_end.P_y;
	    n++;
	}
	if (ap->GA_close == ACT_PIE) {
	    p[2 * n] = ap->GA_center.P_x;
	    p[2 * n + 1] = ap->GA_center.P_y;
	    n++;
	}
	if (!n || p[0] != ap->GA_start.P_x || p[1] != ap->GA_start.P_y) {
	    p[2 * n] = ap->GA_start.P_x;
	    p[2 * n + 1] = ap->GA_start.P_y;
	    n++;
	}
	p[2 * n] = p[0];
	p[2 * n + 1] = p[1];
	n++;
    }
    p[2 * n] = p[2 * n + 1] = (sword) EOREGREC;
    *count = n;
    return (Boolean) (ap->GA_close != ACT_OPEN);
}

void GrDrawArcEllipseLow(const GrDraw *d, sword l, sword t, sword r,
			 sword b, word flags, GrArcParams *ap)
{
    sword *pts, top, bot;
    word count;
    Boolean connected = TRUE;

    if (d->GD_w->W_grFlags & WGF_MASK_NULL)
	return;
    if (flags & GR_CEF_COLINEAR) {		/* l, t to r, b */
	sword x1, y1, x2, y2;
	if (GrTransDev(d, l, t, &x1, &y1) && GrTransDev(d, r, b, &x2, &y2))
	    GrDrawLineLow(d, x1, y1, x2, y2, LINE);
	return;
    }
    pts = GrCalcEllipse(d, l, t, r, b, flags, ap, &count, &top, &bot);
    if (!pts)
	return;
    if ((flags & GR_CEF_TYPE) != GR_CET_ELLIPSE)
	connected = GrCloseArc(&pts, &count, ap);
    if (count >= 2)
	GrDrawPolylineLow(d, pts, count, connected);
    free(pts);
}

/* TempFillPolygonLow: a convex polygon to the driver, others as a
 * region by the odd-even rule */
static void FillPolygon(const GrDraw *d, sword *pts, word count,
			Boolean convex, word attr)
{
    DriverRegs regs;

    if (!convex) {
	GrDrawPolygonDev(d, pts, count, ODD_EVEN, attr);
	return;
    }
    memset(&regs, 0, sizeof(regs));
    regs.DR_cx = count;
    regs.DR_farPtr = pts;
    GrCallDriver(d, DR_VID_POLYGON, &regs, attr);
}

void GrFillArcEllipseLow(const GrDraw *d, sword l, sword t, sword r,
			 sword b, word flags, GrArcParams *ap, word attr)
{
    sword *pts, top, bot;
    word count;
    Boolean arc = (Boolean) ((flags & GR_CEF_TYPE) != GR_CET_ELLIPSE);

    if ((d->GD_w->W_grFlags & WGF_MASK_NULL) || (flags & GR_CEF_COLINEAR))
	return;
    pts = GrCalcEllipse(d, l, t, r, b, flags, ap, &count, &top, &bot);
    if (!pts)
	return;
    if (arc)
	GrCloseArc(&pts, &count, ap);
    if (count > 2)
	FillPolygon(d, pts, count, (Boolean) !arc, attr);
    free(pts);
}

/* FillArcEllipseLow with SetupEllipseLineLow: device coordinates, the
 * line attributes (round line ends and joins) */
void GrFillEllipseDev(const GrDraw *d, sword l, sword t, sword r, sword b,
		      word attr)
{
    GrFillArcEllipseLow(d, l, t, r, b,
			GR_CET_ELLIPSE | GR_CEF_DONT_TRANSFORM, NULL, attr);
}

/* ---------------------------------------------------------------------
 * Bounded arcs: the points at the angles
 * ------------------------------------------------------------------- */

/* MulWWFixed: the product, truncated, as 32.16 (the integer part) */
static dword SqrInt(sdword v)
{
    uint64_t a = (uint64_t) (v < 0 ? -(int64_t) v : v);

    return (dword) ((a * a) >> 32);
}

/* SqrRootDWFixed, of an integer */
static sdword SqrRootInt(dword v)
{
    if (!(v >> 16))
	return (sdword) GrSqrRootWWFixed((WWFixedAsDWord) (v << 16));
    return (sdword) ((dword) GrSqrRootWWFixed((WWFixedAsDWord) v) << 8);
}

/* CalcEllipsePointWithAng: the point of the ellipse in l, t, r, b at
 * angle, WWFixed document coordinates */
static void PointWithAng(sword l, sword t, sword r, sword b, sword angle,
			 sdword *px, sdword *py)
{
    sdword a = (sdword) ((dword) (word) (r - l) << 15);
    sdword bb = (sdword) ((dword) (word) (b - t) << 15);
    sdword cx = (sdword) ((dword) (word) l << 16) + a;
    sdword cy = (sdword) ((dword) (word) t << 16) + bb;
    sdword x, y, tan, root, ratio;

    while (angle < 0)			/* NormalizeAngle */
	angle = (sword) (angle + 360);
    while (angle >= 360)
	angle = (sword) (angle - 360);
    if (angle == 0 || angle == 180) {
	x = angle ? -a : a;
	y = 0;
    } else if (angle == 90 || angle == 270) {
	x = 0;
	y = angle == 90 ? -bb : bb;
    } else {
	tan = (sdword) GrQuickTangent((WWFixedAsDWord) angle << 16);
	root = SqrRootInt(SqrInt((sdword) GrMulWWFixed(tan, a)) +
			  SqrInt(bb));
	ratio = (sdword) GrSDivWWFixed(bb, root);
	x = (sdword) GrMulWWFixed(ratio, a);
	if (angle >= 90 && angle <= 270)
	    x = -x;
	y = -(sdword) GrMulWWFixed(tan, x);
    }
    *px = cx + x;
    *py = cy + y;
}

/* ArcGetAnglePos: rounded, document coordinates */
void GrArcAnglePos(sword l, sword t, sword r, sword b, sword angle,
		   sword *x, sword *y)
{
    sdword px, py;

    PointWithAng(l, t, r, b, angle, &px, &py);
    *x = Round(px);
    *y = Round(py);
}

/* GetEllipsePointWithAng: device coordinates */
static void DevPointWithAng(const GrDraw *d, sword l, sword t, sword r,
			    sword b, sword angle, Point *p)
{
    sdword px, py;
    int64_t x, y;

    PointWithAng(l, t, r, b, angle, &px, &py);
    x = px;
    y = py;
    GrMatTransDWF(&d->GD_w->W_curTMatrix, &x, &y);
    p->P_x = (sword) ((x + 0x8000) >> 16);
    p->P_y = (sword) ((y + 0x8000) >> 16);
}

/* SetupArcLow: the ends of an arc from angle1 to angle2 */
void GrSetupArcLow(const GrDraw *d, sword l, sword t, sword r, sword b,
		   sword angle1, sword angle2, GrArcParams *ap)
{
    DevPointWithAng(d, l, t, r, b, angle2, &ap->GA_endPoint);
    DevPointWithAng(d, l, t, r, b, angle1, &ap->GA_startPoint);
}

/* ---------------------------------------------------------------------
 * 3-point arcs
 * ------------------------------------------------------------------- */

static void Trans(const GrDraw *d, const PointWWFixed *p, sdword *x,
		  sdword *y)
{
    int64_t vx = WWF(&p->PF_x), vy = WWF(&p->PF_y);

    GrMatTransDWF(&d->GD_w->W_curTMatrix, &vx, &vy);
    *x = (sdword) vx;
    *y = (sdword) vy;
}

/* Order3PointsInArc: points 1 and 3 swapped if the arc from 1 through 2
 * to 3 would run clockwise on the device */
static void Order3Points(const GrDraw *d, GrArcParams *ap)
{
    sdword x1, y1, x2, y2, x3, y3, m, c;
    WWFixedAsDWord q;
    int si = 0;
    PointWWFixed s;

    Trans(d, &ap->GA_point1, &x1, &y1);
    Trans(d, &ap->GA_point2, &x2, &y2);
    Trans(d, &ap->GA_point3, &x3, &y3);
    if (GrSDiv32((WWFixedAsDWord) (y3 - y1), (WWFixedAsDWord) (x3 - x1),
		 &q)) {
	m = (sdword) q;
	c = -((sdword) GrMulWWFixed(m, x1) - y1);
	if (y2 < c + (sdword) GrMulWWFixed(m, x2))
	    si++;
	if (x1 > x3 || (x1 == x3 && y1 <= y3))
	    si++;
    } else {				/* vertical */
	if (y1 >= y3)
	    si++;
	if (x2 >= x3)
	    si++;
    }
    if (si & 1) {
	s = ap->GA_point1;
	ap->GA_point1 = ap->GA_point3;
	ap->GA_point3 = s;
    }
}

/* CalcInverseSlopeAndStore */
static sdword InverseSlope(sdword dy, sdword dx)
{
    return dy ? (sdword) GrSDivWWFixed(dx, dy) : INFINITE_SLOPE;
}

/* Convert3PointToBounds: FALSE when the points are on a line */
static Boolean Convert3Point(GrArcParams *ap, sword *l, sword *t, sword *r,
			     sword *b)
{
    sdword p1x = WWF(&ap->GA_point1.PF_x), p1y = WWF(&ap->GA_point1.PF_y);
    sdword p2x = WWF(&ap->GA_point2.PF_x), p2y = WWF(&ap->GA_point2.PF_y);
    sdword p3x = WWF(&ap->GA_point3.PF_x), p3y = WWF(&ap->GA_point3.PF_y);
    sdword x1 = (p1x + p2x) >> 1, y1 = (p1y + p2y) >> 1;
    sdword y2 = (p2y + p3y) >> 1, x2 = (p2x + p3x) >> 1;
    sdword m1 = InverseSlope(p1y - p2y, p2x - p1x);
    sdword m2 = InverseSlope(p2y - p3y, p3x - p2x);
    sdword c2, x0, y0, dx, dy, rad, s;

    /* CalcLineLineIntersection */
    if (m1 == m2) {
	*l = Round(p1x);
	*t = Round(p1y);
	*r = Round(p3x);
	*b = Round(p3y);
	return FALSE;
    }
    if (m2 == INFINITE_SLOPE) {
	m2 = m1; m1 = INFINITE_SLOPE;
	s = x1; x1 = x2; x2 = s;
	s = y1; y1 = y2; y2 = s;
    }
    c2 = y2 - (sdword) GrMulWWFixed(m2, x2);
    if (m1 == INFINITE_SLOPE)
	x0 = x1;
    else
	x0 = (sdword) GrSDivWWFixed((sdword) GrMulWWFixed(m1, x1) - y1 + c2,
				    m1 - m2);
    y0 = (sdword) GrMulWWFixed(m2, x0) + c2;
    SetWWF(&ap->GA_point2.PF_x, x0);
    SetWWF(&ap->GA_point2.PF_y, y0);

    /* CalcPointPointDistance: to point 3 */
    dx = x0 - p3x;
    if (dx < 0)
	dx = -dx;
    dy = y0 - p3y;
    if (dy < 0)
	dy = -dy;
    if (!dy)
	rad = dx;
    else if (!dx)
	rad = dy;
    else {
	/* SqrWWFixed of each, added as 32.16, SqrRootDWFixed */
	uint64_t sx = (uint64_t) dx * (uint64_t) dx >> 16;
	uint64_t sy = (uint64_t) dy * (uint64_t) dy >> 16;
	uint64_t sum = sx + sy;		/* 32.16 */
	if (!(sum >> 32))
	    rad = (sdword) GrSqrRootWWFixed((WWFixedAsDWord) sum);
	else
	    rad = (sdword) ((dword) GrSqrRootWWFixed(
				(WWFixedAsDWord) (sum >> 16)) << 8);
    }
    *l = Round(x0 - rad);
    *t = Round(y0 - rad);
    *r = Round(x0 + rad);
    *b = Round(y0 + rad);
    return TRUE;
}

/* SetupArc3PointLow: points in order, the bounds in l, t, r, b; the
 * CalcEllipse flags */
word GrSetupArc3PointLow(const GrDraw *d, GrArcParams *ap, sword *l,
			 sword *t, sword *r, sword *b)
{
    Order3Points(d, ap);
    if (!Convert3Point(ap, l, t, r, b))
	return GR_CET_3PT_ARC | GR_CEF_COLINEAR;
    return GR_CET_3PT_ARC;
}
