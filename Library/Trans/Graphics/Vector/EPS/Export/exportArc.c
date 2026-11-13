/*
 * exportArc.c -- three point arcs as PostScript arcs (the C port of
 * exportArc.asm).
 *
 * PostScript knows arcs by their bounds and two angles; the three point
 * forms are turned into those: the circle through the points (the
 * perpendicular bisectors of the two chords meet at its center), its
 * bounds, and the angles of the first and last point.
 */
#include "exportInt.h"

#define INFINITE_SLOPE  0x7fffffffL

/* GrSDivWWFixed with the overflow the asm saw in the carry */
static Boolean SDiv(sdword a, sdword b, sdword *q)
{
    int64_t r;

    if (!b)
	return FALSE;
    r = ((int64_t) a * 65536) / b;
    if (r > 0x7fffffffL || r < -0x7fffffffL - 1)
	return FALSE;
    *q = (sdword) r;
    return TRUE;
}

static sdword Mul(sdword a, sdword b)
{
    return (sdword) GrMulWWFixed((WWFixedAsDWord) a, (WWFixedAsDWord) b);
}

static sdword PtWW(const WWFixed *w)
{
    return (sdword) (((dword) (word) w->WWF_int << 16) | w->WWF_frac);
}

/* the points: Setup3PointArc, Setup3PointArcTo, Setup3PointRelArcTo */
static void SetupPoints(TGSLocals *tgs, const byte *e)
{
    EPSArc3 *a = &tgs->TGS_arc;
    PointWWFixed cp;

    a->close = (word) EL_W(e, 1);
    if (e[0] == GR_DRAW_ARC_3POINT || e[0] == GR_FILL_ARC_3POINT) {
	a->x1 = EL_D(e, 3);  a->y1 = EL_D(e, 7);
	a->x2 = EL_D(e, 11); a->y2 = EL_D(e, 15);
	a->x3 = EL_D(e, 19); a->y3 = EL_D(e, 23);
	return;
    }
    GrGetCurPosWWFixed(tgs->TGS_pathGState ? tgs->TGS_pathGState
					   : tgs->TGS_gstate, &cp);
    a->x1 = (WWFixedAsDWord) PtWW(&cp.PF_x);
    a->y1 = (WWFixedAsDWord) PtWW(&cp.PF_y);
    if (e[0] == GR_DRAW_REL_ARC_3POINT_TO) {
	/* the asm stored the second point into the first (Port32 fix:
	 * point 2 is the pen plus the first offset, point 3 that plus the
	 * second) */
	a->x2 = a->x1 + EL_D(e, 3);
	a->y2 = a->y1 + EL_D(e, 7);
	a->x3 = a->x2 + EL_D(e, 11);
	a->y3 = a->y2 + EL_D(e, 15);
    } else {
	a->x2 = EL_D(e, 3);  a->y2 = EL_D(e, 7);
	a->x3 = EL_D(e, 11); a->y3 = EL_D(e, 15);
    }
}

/* Order3PointsInArc: point 1 first, counterclockwise from 0 degrees */
static void Order3PointsInArc(TGSLocals *tgs)
{
    EPSArc3 *a = &tgs->TGS_arc;
    sdword x1 = (sdword) a->x1, y1 = (sdword) a->y1,
	x2 = (sdword) a->x2, y2 = (sdword) a->y2,
	x3 = (sdword) a->x3, y3 = (sdword) a->y3;
    sdword slope, b;
    int swaps = 0;

    if (SDiv(y3 - y1, x3 - x1, &slope)) {
	b = -(Mul(slope, x1) - y1);
	if (y2 < b + Mul(slope, x2))
	    swaps++;			/* point 2 below the line */
	if (x1 < x3)
	    ;
	else if (x1 > x3 || !(y1 > y3))
	    swaps++;
    } else {				/* vertical */
	if (!(y1 < y3))
	    swaps++;
	if (!(x2 >= x3))
	    swaps--;
	swaps++;
    }
    if (!(swaps & 1)) {
	a->x1 = (WWFixedAsDWord) x3;
	a->y1 = (WWFixedAsDWord) y3;
	a->x3 = (WWFixedAsDWord) x1;
	a->y3 = (WWFixedAsDWord) y1;
    }
}

static sdword CalcInverseSlope(sdword negDY, sdword dX)
{
    sdword q;

    if (!negDY)
	return INFINITE_SLOPE;
    return SDiv(negDY, dX, &q) ? q : 0;
}

/* the integer square root of a 64-bit value */
static dword ISqrt64(uint64_t v)
{
    uint64_t r = 0, bit = (uint64_t) 1 << 62;

    while (bit > v)
	bit >>= 2;
    while (bit) {
	if (v >= r + bit) {
	    v -= r + bit;
	    r = (r >> 1) + bit;
	} else
	    r >>= 1;
	bit >>= 2;
    }
    return (dword) r;
}

/* Convert3PointToBounds, CalcLineLineIntersection, CalcPointPointDistance */
static void Convert3PointToBounds(TGSLocals *tgs)
{
    EPSArc3 *a = &tgs->TGS_arc;
    EPSNewArc *n = &tgs->TGS_newArc;
    sdword b2, x0, y0, dx, dy, r, t;

    n->p1x = (WWFixedAsDWord) (((sdword) a->x1 + (sdword) a->x2) >> 1);
    n->p1y = (WWFixedAsDWord) (((sdword) a->y1 + (sdword) a->y2) >> 1);
    n->p2y = (WWFixedAsDWord) (((sdword) a->y2 + (sdword) a->y3) >> 1);
    n->p2x = (WWFixedAsDWord) (((sdword) a->x2 + (sdword) a->x3) >> 1);
    n->m1 = (WWFixedAsDWord) CalcInverseSlope(
	(sdword) a->y1 - (sdword) a->y2, (sdword) a->x2 - (sdword) a->x1);
    n->m2 = (WWFixedAsDWord) CalcInverseSlope(
	(sdword) a->y2 - (sdword) a->y3, (sdword) a->x3 - (sdword) a->x2);

    /* the center: y = mx + b for both bisectors */
    if ((sdword) n->m2 == INFINITE_SLOPE) {
	t = (sdword) n->m1; n->m1 = n->m2; n->m2 = (WWFixedAsDWord) t;
	t = (sdword) n->p1x; n->p1x = n->p2x; n->p2x = (WWFixedAsDWord) t;
	t = (sdword) n->p1y; n->p1y = n->p2y; n->p2y = (WWFixedAsDWord) t;
    }
    b2 = (sdword) n->p2y - Mul((sdword) n->m2, (sdword) n->p2x);
    x0 = (sdword) n->p1x;
    if ((sdword) n->m1 != INFINITE_SLOPE) {
	/* b1 = y1 - m1 x1, x0 = (b2 - b1) / (m1 - m2) */
	sdword num = Mul((sdword) n->m1, (sdword) n->p1x) -
	    (sdword) n->p1y + b2;
	if (!SDiv(num, (sdword) n->m1 - (sdword) n->m2, &x0))
	    x0 = 0;
    }
    y0 = Mul(x0, (sdword) n->m2) + b2;

    /* the radius: from the center to point 3 */
    dx = x0 - (sdword) a->x3;
    dy = y0 - (sdword) a->y3;
    if (dx < 0)
	dx = -dx;
    if (dy < 0)
	dy = -dy;
    if (!dy)
	r = dx;
    else if (!dx)
	r = dy;
    else
	r = (sdword) ISqrt64((uint64_t) ((int64_t) dx * dx) +
			     (uint64_t) ((int64_t) dy * dy));

    n->radius = (WWFixedAsDWord) r;
    n->cx = (WWFixedAsDWord) x0;
    n->cy = (WWFixedAsDWord) y0;
    n->bry = (WWFixedAsDWord) (y0 + r);
    n->brx = (WWFixedAsDWord) (x0 + r);
    n->tly = (WWFixedAsDWord) (y0 - r);
    n->tlx = (WWFixedAsDWord) (x0 - r);
}

static void CalcArcAngles(TGSLocals *tgs)
{
    EPSArc3 *a = &tgs->TGS_arc;
    EPSNewArc *n = &tgs->TGS_newArc;
    sdword q, dx;

    if (!SDiv((sdword) a->y1 - (sdword) n->cy, (sdword) n->radius, &q))
	q = 0;
    dx = (sdword) a->x1 - (sdword) n->cx;
    n->ang1 = GrQuickArcSine((WWFixedAsDWord) q, (word) (dx >> 16));
    if (!SDiv((sdword) a->y3 - (sdword) n->cy, (sdword) n->radius, &q))
	q = 0;
    dx = (sdword) a->x3 - (sdword) n->cx;
    n->ang2 = GrQuickArcSine((WWFixedAsDWord) q, (word) (dx >> 16));
}

/* Convert3PointToNormalArc: "ang1 ang2 x2 x1 y2 y1 " of the arc's
 * circle into TGS_buffer (restarted) */
void Convert3PointToNormalArc(TGSLocals *tgs, const byte *e,
			      GStateHandle gs)
{
    EPSNewArc *n = &tgs->TGS_newArc;

    (void) gs;
    SetupPoints(tgs, e);
    Order3PointsInArc(tgs);
    Convert3PointToBounds(tgs);
    CalcArcAngles(tgs);

    ResetBuffer(tgs);
    tgs->p = WWFixedToAscii(tgs->p, n->ang1);
    *tgs->p++ = ' ';
    tgs->p = WWFixedToAscii(tgs->p, n->ang2);
    *tgs->p++ = ' ';
    tgs->p = WWFixedToAscii(tgs->p, tgs->TGS_xfactor ? n->brx : 0);
    *tgs->p++ = ' ';
    tgs->p = WWFixedToAscii(tgs->p, tgs->TGS_xfactor ? n->tlx : 0);
    *tgs->p++ = ' ';
    tgs->p = WWFixedToAscii(tgs->p, tgs->TGS_yfactor ? n->bry : 0);
    *tgs->p++ = ' ';
    tgs->p = WWFixedToAscii(tgs->p, tgs->TGS_yfactor ? n->tly : 0);
    *tgs->p++ = ' ';
}
