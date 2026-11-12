/*
 * graphicsCalcEllipse.c -- the points of an ellipse or elliptical arc
 * (after graphicsCalcEllipse.asm: CalcEllipse, EllipseTrivialReject,
 * ConjugateEllipse, SetupEllipse, SimpleEllipse, SimpleArc,
 * TransCoordAndDouble, AvgTwoCoords, CheckXMinMax, CheckYMinMax,
 * MulSignedDWord, TestRotation, SetupArc, Setup3PointArc, SetupArcCommon,
 * TestFlip, PointToOctant).
 *
 * The bounding box is transformed (unless CEF_DONT_TRANSFORM) and its
 * coordinates doubled, so that a center between pixels is a whole
 * number.  The ellipse is then given by its center J and two conjugate
 * radii P and Q (with rotation, from the midpoints of the transformed
 * box's sides), and from them the coefficients of its equation with the
 * origin at J + P, where CalcConic (graphicsCalcConic.c) starts.  An
 * ellipse at most two pixels thick along one side is just its four
 * extreme points (SimpleEllipse).
 *
 * An arc is the same ellipse, from its start to its end point (device
 * coordinates; swapped when the matrix mirrors), which CalcConic finds
 * on the curve.  A 3-point arc brings its own center.
 */
#include "graphicsInt.h"
#include <stdlib.h>

typedef struct {
    Point	p1, p2, p3, p4;		/* the box's corners, doubled */
    Point	J;			/* center */
    Point	P, Q;			/* conjugate radii */
    sdword	centerX, centerY;	/* EF_center, WWFixed, not doubled */
    GrConic	conic;
} Frame;

static Boolean Double(sword *v)
{
    if (*v > 0x3fff || *v < -0x4000)
	return FALSE;
    *v = (sword) (*v * 2);
    return TRUE;
}

/* TransCoordAndDouble: two points */
static Boolean TransDouble(const GrDraw *d, word flags, sword *ax, sword *bx,
			   sword *cx, sword *dx)
{
    if (!(flags & GR_CEF_DONT_TRANSFORM) &&
	(!GrTransDev(d, *ax, *bx, ax, bx) || !GrTransDev(d, *cx, *dx, cx, dx)))
	return FALSE;
    return (Boolean) (Double(ax) && Double(bx) && Double(cx) && Double(dx));
}

/* AvgTwoCoords */
static void Avg(sword ax, sword bx, sword *cx, sword *dx)
{
    *cx = (sword) (ax + ((sword) (*cx - ax) >> 1));
    *dx = (sword) (bx + ((sword) (*dx - bx) >> 1));
}

static Boolean Rotated(const GrDraw *d)
{
    return (Boolean) ((d->GD_w->W_curTMatrix.TM_flags & TM_ROTATED) != 0);
}

/* EllipseTrivialReject: C_bounds (doubled), and the bounds (not
 * doubled) with the top and bottom clipped to the mask; TRUE: outside */
static Boolean TrivialReject(const GrDraw *d, Frame *f, Rectangle *r)
{
    sword ax = f->p1.P_x, bx = f->p1.P_y, cx, dx;
    const Point *p[3];
    int i;

    if (!Rotated(d)) {
	cx = f->p3.P_x;
	dx = f->p3.P_y;
	if (ax > cx) {
	    sword s = ax; ax = cx; cx = s;
	}
	if (bx > dx) {
	    sword s = bx; bx = dx; dx = s;
	}
    } else {
	cx = ax;
	dx = bx;
	p[0] = &f->p2; p[1] = &f->p3; p[2] = &f->p4;
	for (i = 0; i < 3; i++) {
	    if (ax >= p[i]->P_x)		/* CheckXMinMax */
		ax = p[i]->P_x;
	    else if (cx < p[i]->P_x)
		cx = p[i]->P_x;
	    if (bx >= p[i]->P_y)		/* CheckYMinMax */
		bx = p[i]->P_y;
	    else if (dx < p[i]->P_y)
		dx = p[i]->P_y;
	}
    }
    f->conic.C_bounds.R_left = ax;
    f->conic.C_bounds.R_top = bx;
    f->conic.C_bounds.R_right = cx;
    f->conic.C_bounds.R_bottom = dx;
    ax >>= 1; bx >>= 1; cx >>= 1; dx >>= 1;
    if (d->GD_w) {
	const Rectangle *m = &d->GD_w->W_maskRect;
	if (ax > m->R_right || cx < m->R_left || bx > m->R_bottom ||
	    dx < m->R_top)
	    return TRUE;
	if (bx < m->R_top)
	    bx = m->R_top;
	if (dx > m->R_bottom)
	    dx = m->R_bottom;
    }
    r->R_left = ax;
    r->R_top = bx;
    r->R_right = cx;
    r->R_bottom = dx;
    return FALSE;
}

/* ConjugateEllipse: the coefficients from P and Q; FALSE when they are
 * parallel */
static Boolean Conjugate(Frame *f)
{
    GrConic *c = &f->conic;
    sdword px = f->P.P_x, py = f->P.P_y, qx = f->Q.P_x, qy = f->Q.P_y;
    sdword det = px * qy - py * qx;

    if (!det)
	return FALSE;
    det = (sdword) ((dword) det << 1);
    c->C_D = (int64_t) det * qy;
    c->C_E = -(int64_t) det * qx;
    c->C_A = (int64_t) (py * py) + (int64_t) (qy * qy);
    c->C_B = -2 * ((int64_t) (px * py) + (int64_t) (qx * qy));
    c->C_C = (int64_t) (px * px) + (int64_t) (qx * qx);
    c->C_F = 0;
    return TRUE;
}

/* SetupEllipse */
static void SetupEllipse(Frame *f)
{
    f->conic.C_beg.P_x = f->conic.C_end.P_x = (sword) (f->P.P_x + f->J.P_x);
    f->conic.C_beg.P_y = f->conic.C_end.P_y = (sword) (f->P.P_y + f->J.P_y);
    f->conic.C_conicType = GR_CT_ELLIPSE;
}

/* PointToOctant: the octant a vector points into (as GetOctant) */
static int Octant(sdword x, sdword y)
{
    if (x >= 0) {
	if (y >= 0)
	    return x >= y ? 7 : 8;
	return x <= -y ? 5 : 6;
    }
    if (y >= 0)
	return -x <= y ? 1 : 2;
    return -x >= -y ? 3 : 4;
}

/* TestFlip: TRUE when the matrix mirrors, so that an arc's ends trade
 * places (the octants of (1,1) and (-1,1) through its 2x2 part) */
Boolean GrTestFlip(const GrDraw *d)
{
    const TMatrix *m = &d->GD_w->W_curTMatrix;
    int64_t x1 = 1L << 16, y1 = 1L << 16, x2 = -(1L << 16), y2 = 1L << 16;
    int a, b;

    if (m->TM_flags & TM_COMPLEX) {
	TMatrix c = *m;
	memset(&c.TM_31, 0, sizeof(c.TM_31));
	memset(&c.TM_32, 0, sizeof(c.TM_32));
	GrMatTransDWF(&c, &x1, &y1);
	GrMatTransDWF(&c, &x2, &y2);
    }
    a = Octant((sdword) x1, (sdword) y1);
    b = Octant((sdword) x2, (sdword) y2);
    a -= b;
    return (Boolean) (a > 0 ? a < 4 : a < -4);
}

static sword Round(sdword v)
{
    return (sword) ((v + 0x8000L) >> 16);
}

/* SetupArcCommon: the arc's ends, device coordinates */
static void SetupArcCommon(Frame *f, word type, GrArcParams *ap, sword ax,
			   sword bx, sword cx, sword dx)
{
    f->conic.C_conicBeg.P_x = ap->GA_start.P_x = ax;
    f->conic.C_conicBeg.P_y = ap->GA_start.P_y = bx;
    f->conic.C_conicEnd.P_x = ap->GA_end.P_x = cx;
    f->conic.C_conicEnd.P_y = ap->GA_end.P_y = dx;
    SetupEllipse(f);
    f->conic.C_conicInfo = GR_CAI_FIND_START;
    f->conic.C_conicType = GR_CT_ARC;
    if (type != GR_CET_BOUNDED_ARC_RR) {
	ap->GA_center.P_x = Round(f->centerX);
	ap->GA_center.P_y = Round(f->centerY);
    }
}

/* SetupArc: a bounded arc, its ends in device coordinates */
static void SetupArc(const GrDraw *d, Frame *f, word type, GrArcParams *ap)
{
    if (GrTestFlip(d))
	SetupArcCommon(f, type, ap, ap->GA_endPoint.P_x, ap->GA_endPoint.P_y,
		       ap->GA_startPoint.P_x, ap->GA_startPoint.P_y);
    else
	SetupArcCommon(f, type, ap, ap->GA_startPoint.P_x,
		       ap->GA_startPoint.P_y, ap->GA_endPoint.P_x,
		       ap->GA_endPoint.P_y);
}

static void TransFixed(const GrDraw *d, const PointWWFixed *p, sdword *x,
		       sdword *y)
{
    int64_t vx = (sdword) (((dword) (word) p->PF_x.WWF_int << 16) |
			   p->PF_x.WWF_frac);
    int64_t vy = (sdword) (((dword) (word) p->PF_y.WWF_int << 16) |
			   p->PF_y.WWF_frac);

    GrMatTransDWF(&d->GD_w->W_curTMatrix, &vx, &vy);
    *x = (sdword) vx;
    *y = (sdword) vy;
}

/* Setup3PointArc: the center from point2, the ends from points 1 and 3 */
static void Setup3PointArc(const GrDraw *d, Frame *f, word type,
			   GrArcParams *ap)
{
    sdword x1, y1, x3, y3;

    TransFixed(d, &ap->GA_point2, &f->centerX, &f->centerY);
    TransFixed(d, &ap->GA_point1, &x1, &y1);
    TransFixed(d, &ap->GA_point3, &x3, &y3);
    SetupArcCommon(f, type, ap, Round(x1), Round(y1), Round(x3), Round(y3));
}

/* SimpleEllipse: the ends of the radii, P, -Q, -P, Q and P again */
static word SimpleEllipse(const Frame *f, sword *o)
{
    static const signed char sp[5] = { 1, 0, -1, 0, 1 };
    static const signed char sq[5] = { 0, -1, 0, 1, 0 };
    int i;

    for (i = 0; i < 5; i++) {		/* WriteSimplePoint */
	*o++ = (sword) ((sword) (sp[i] * f->P.P_x + sq[i] * f->Q.P_x +
				 f->J.P_x) >> 1);
	*o++ = (sword) ((sword) (sp[i] * f->P.P_y + sq[i] * f->Q.P_y +
				 f->J.P_y) >> 1);
    }
    return 5;
}

/* checkTooThin: two corners within two pixels of each other */
static Boolean Near(const Point *a, const Point *b)
{
    sword x = (sword) (a->P_x - b->P_x), y = (sword) (a->P_y - b->P_y);

    if (x < 0)
	x = (sword) -x;
    if (y < 0)
	y = (sword) -y;
    return (Boolean) (x <= 4 && y <= 4);
}

/*
 * CalcEllipse: the bounds l, t, r, b (document coordinates, or device
 * ones with GR_CEF_DONT_TRANSFORM); flags: GR_CET_ and GR_CEF_; ap: an
 * arc's parameters.  The points in the separator format (C heap, *count
 * of them, EOREGREC twice after), the rows they were computed for in
 * *top and *bot; NULL when there is nothing to draw.
 */
sword *GrCalcEllipse(const GrDraw *d, sword l, sword t, sword r, sword b,
		     word flags, GrArcParams *ap, word *count, sword *top,
		     sword *bot)
{
    Frame f;
    Rectangle rb;
    sword ax, bx, cx, dx, s;
    Boolean thin;
    word w, type = (word) (flags & GR_CEF_TYPE);
    sword *buf;

    memset(&f, 0, sizeof(f));
    if (l > r) {
	s = l; l = r; r = s;
    }
    if (t > b) {
	s = t; t = b; b = s;
    }
    ax = l; bx = t; cx = r; dx = b;
    if (!TransDouble(d, flags, &ax, &bx, &cx, &dx))
	return NULL;
    f.p1.P_x = ax; f.p1.P_y = bx;
    f.p2.P_x = cx; f.p2.P_y = bx;
    f.p3.P_x = cx; f.p3.P_y = dx;
    f.p4.P_x = ax; f.p4.P_y = dx;
    Avg(ax, bx, &cx, &dx);
    f.J.P_x = f.conic.C_center.P_x = cx;
    f.J.P_y = f.conic.C_center.P_y = dx;
    f.centerX = (sdword) cx << 15;
    f.centerY = (sdword) dx << 15;
    if (!(flags & GR_CEF_DONT_TRANSFORM) && Rotated(d)) {
	/* the radii to the midpoints of the transformed box's sides */
	ax = r; bx = t; cx = l; dx = b;
	if (!TransDouble(d, flags, &ax, &bx, &cx, &dx))
	    return NULL;
	f.p2.P_x = ax; f.p2.P_y = bx;
	f.p4.P_x = cx; f.p4.P_y = dx;
	Avg(f.p3.P_x, f.p3.P_y, &cx, &dx);
	f.Q.P_x = (sword) (cx - f.J.P_x);
	f.Q.P_y = (sword) (dx - f.J.P_y);
	cx = f.p2.P_x; dx = f.p2.P_y;
	Avg(f.p3.P_x, f.p3.P_y, &cx, &dx);
	f.P.P_x = (sword) (cx - f.J.P_x);
	f.P.P_y = (sword) (dx - f.J.P_y);
    } else {
	f.P.P_x = (sword) (f.p2.P_x - f.J.P_x);
	f.P.P_y = 0;
	f.Q.P_x = 0;
	f.Q.P_y = (sword) (f.p3.P_y - f.J.P_y);
    }

    thin = (Boolean) (Near(&f.p1, &f.p2) || Near(&f.p2, &f.p3));
    if (type == GR_CET_ELLIPSE)
	SetupEllipse(&f);
    else if (type == GR_CET_3PT_ARC)
	Setup3PointArc(d, &f, type, ap);
    else
	SetupArc(d, &f, type, ap);
    if (TrivialReject(d, &f, &rb))
	return NULL;
    {
	sdword lw = (sdword) (((dword) (word) d->GD_gs->GS_lineWidth.WWF_int
			       << 16) | d->GD_gs->GS_lineWidth.WWF_frac);
	w = (word) (GrScaleScalar(d, lw) + 1);
    }
    f.conic.C_winTop = (sword) (rb.R_top - w);
    f.conic.C_winBot = (sword) (rb.R_bottom + w);
    *top = f.conic.C_winTop;
    *bot = f.conic.C_winBot;

    if (!Conjugate(&f) || thin) {		/* colinear */
	buf = malloc(12 * sizeof(sword));
	if (!buf)
	    return NULL;
	/* SimpleArc: nothing (the asm left the count it had) */
	*count = type == GR_CET_ELLIPSE ? SimpleEllipse(&f, buf) : 0;
	buf[2 * *count] = buf[2 * *count + 1] = (sword) EOREGREC;
	return buf;
    }
    return GrCalcConic(&f.conic, count);
}
