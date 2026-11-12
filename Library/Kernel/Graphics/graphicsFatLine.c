/*
 * graphicsFatLine.c -- lines wider than a pixel (after
 * graphicsFatLine.asm: DoFatLines, GetContiguousPoints, DoFatLinesLow,
 * CheckForSpecialCases, RemovePointRedundancies, DrawFatPolyLine,
 * HandleFatPolyConnection, DrawFatPolyLineLow, BasicFatLineCalcs,
 * DrawFatLineReg, HandleMiteredConnection, CalcJoin, CalcBeveledJoin,
 * CheckMiterLimit, CalcMiteredJoin, CalcLineAngle, CalcCCWAngleDiff,
 * MakeEndCap, DrawRoundCap, MakeButtCap, MakeSquareCap).
 *
 * Every segment of a polyline (device coordinates, the separator
 * format) becomes a polygon of six points, filled with the line
 * attributes (graphicsPolygon.c):
 *     secondA, secondExtra, secondB, firstB, firstExtra, firstA
 * A and B are the two sides, the width (GS_scaledLineWid - 1) split
 * between them; Extra is the corner of a bevel.  A segment is drawn
 * once the join with the next one has moved its second end:
 *   - ends: butt (the sides end at the point), square (half the width
 *     further), round (a filled circle, graphicsArc.c);
 *   - joins: beveled (the gap filled by a triangle), mitered (the sides
 *     meet; beveled when the angle is under the miter limit), round (a
 *     circle at the point).
 * The arithmetic is the asm's, in WWFixed: the deltas are halved until
 * both are within 181 (their squares fit), the perpendicular is
 * (w - 1) / distance times the deltas, angles come from the quick
 * sine tables (graphicsMath.c).
 *
 * Polylines of the separator format beyond the first are drawn open,
 * as in the asm.
 */
#include "graphicsInt.h"
#include <stdlib.h>

#define MAX_DELTA	181

#define MEC_FIRST	TRUE		/* the first end of the segment */
#define MEC_SECOND	FALSE

#define DFL_CONNECTED		0x01
#define DFL_FIRST_LINE		0x02
#define DFL_CONNECTION_BEVELED	0x04
#define DFL_SAVED_FIRST_B	0x08

#define ANGLE_UNSET	((sdword) 0xffff0000UL)

typedef struct {
    Point	pts[6];		/* secondA, secondExtra, secondB, firstB,
				 * firstExtra, firstA */
    Point	origFirst, origSecond;
    sdword	deltaY, deltaX, distance;	/* WWFixed */
    sdword	kdx, kdy;
    sword	kdxHigh, kdxLow, kdyHigh, kdyLow;
    sdword	angle, sine, cosine;
    sdword	joinDeltaY, joinDeltaX;
} FatLine;

#define SECOND_A	pts[0]
#define SECOND_EXTRA	pts[1]
#define SECOND_B	pts[2]
#define FIRST_B		pts[3]
#define FIRST_EXTRA	pts[4]
#define FIRST_A		pts[5]

typedef struct {
    const GrDraw *d;
    FatLine	fl[2];
    FatLine	*prev, *curr;
    const Point	*pts;
    int		firstPt, firstEnd, secondEnd;	/* point indices */
    Point	savedFirst;
    byte	flags;
} Fat;

#define WWF(i)	((sdword) ((dword) (word) (i) << 16))

/* Round: to the nearer integer, halves away from zero */
static sword Round(sdword v)
{
    word frac = (word) v;
    sword i = (sword) (v >> 16);

    if (frac > 0x8000 || (frac == 0x8000 && i >= 0))
	i++;
    return i;
}

/* Average */
static sword Average(sdword a, sdword b)
{
    return Round((sdword) ((dword) a + (dword) b) >> 1);
}

static sword Width1(const GrDraw *d)		/* the width less one */
{
    return (sword) (d->GD_gs->GS_scaledLineWid - 1);
}

/* BasicFatLineCalcs: the segment from firstEnd to secondEnd into curr */
static void BasicCalcs(Fat *f)
{
    FatLine *c = f->curr;
    Point a = f->pts[f->firstEnd++], b = f->pts[f->secondEnd++];
    sdword dx, dy, w = WWF(Width1(f->d)), ratio;
    int i;

    for (i = 0; i < 3; i++) {
	c->pts[i] = b;
	c->pts[i + 3] = a;
    }
    c->origFirst = a;
    c->origSecond = b;
    dy = (sword) (b.P_y - a.P_y);
    dx = (sword) (b.P_x - a.P_x);
    c->kdx = c->kdy = 0;
    c->angle = ANGLE_UNSET;
    if (!dy) {				/* horizontal */
	c->sine = 0;
	if (dx < 0) {
	    c->kdy = -w;
	    c->angle = WWF(180);
	    c->cosine = WWF(-1);
	} else {
	    c->kdy = w;
	    c->angle = 0;
	    c->cosine = WWF(1);
	}
    } else if (!dx) {			/* vertical */
	c->cosine = 0;
	if (dy < 0) {
	    c->kdx = -w;
	    c->angle = WWF(90);
	    c->sine = WWF(1);
	} else {
	    c->kdx = w;
	    c->angle = WWF(270);
	    c->sine = WWF(-1);
	}
    } else {
	dx = WWF(dx);
	dy = WWF(dy);
	while ((sword) (dx >> 16) > MAX_DELTA ||
	       (sword) (dx >> 16) < -MAX_DELTA ||
	       (sword) (dy >> 16) > MAX_DELTA ||
	       (sword) (dy >> 16) <= -MAX_DELTA) {
	    dx >>= 1;
	    dy >>= 1;
	}
	c->deltaY = dy;
	c->deltaX = dx;
	c->distance = (sdword) GrSqrRootWWFixed(
	    (WWFixedAsDWord) ((dword) GrSqrWWFixed((WWFixedAsDWord) dy) +
			      (dword) GrSqrWWFixed((WWFixedAsDWord) dx)));
	ratio = (sdword) GrUDivWWFixed((WWFixedAsDWord) w,
				       (WWFixedAsDWord) c->distance);
	c->kdx = (sdword) GrMulWWFixed((WWFixedAsDWord) ratio,
				       (WWFixedAsDWord) dy);
	c->kdy = (sdword) GrMulWWFixed((WWFixedAsDWord) ratio,
				       (WWFixedAsDWord) dx);
    }
    {
	sword r = Round(c->kdx);
	c->kdxHigh = Round(WWF(r) >> 1);
	c->kdxLow = (sword) (r - c->kdxHigh);
	r = Round(c->kdy);
	c->kdyHigh = Round(WWF(r) >> 1);
	c->kdyLow = (sword) (r - c->kdyHigh);
    }
}

/* MakeButtCap: the sides at the end, the width split between them */
static void ButtCap(Fat *f, Boolean first)
{
    FatLine *c = f->curr;
    sword cx = c->kdxHigh, ax = c->kdxLow, si = c->kdyLow, bx = c->kdyHigh,
	  s;
    Point *a, *e, *b;

    if (cx < 0) {
	if (bx < 0) {
	    s = bx; bx = si; si = s;
	}
	s = ax; ax = cx; cx = s;
    } else if (bx < 0) {
	s = bx; bx = si; si = s;
    }
    if (first) {
	a = &c->FIRST_A; e = &c->FIRST_EXTRA; b = &c->FIRST_B;
    } else {
	a = &c->SECOND_A; e = &c->SECOND_EXTRA; b = &c->SECOND_B;
    }
    a->P_x = (sword) (a->P_x + ax);
    a->P_y = (sword) (a->P_y - bx);
    e->P_x = (sword) (e->P_x + ax);
    e->P_y = (sword) (e->P_y - bx);
    b->P_x = (sword) (b->P_x - cx);
    b->P_y = (sword) (b->P_y + si);
}

/* MakeSquareCap: half the width further along the line */
static void SquareCap(Fat *f, Boolean first)
{
    FatLine *c = f->curr;
    sword bx = Round(c->kdy >> 1), si = Round(c->kdx >> 1);
    int i;

    if (first)
	for (i = 3; i < 6; i++) {
	    c->pts[i].P_x = (sword) (c->pts[i].P_x - bx);
	    c->pts[i].P_y = (sword) (c->pts[i].P_y - si);
	}
    else
	for (i = 0; i < 3; i++) {
	    c->pts[i].P_x = (sword) (c->pts[i].P_x + bx);
	    c->pts[i].P_y = (sword) (c->pts[i].P_y + si);
	}
    ButtCap(f, first);
}

/* DrawRoundCap: a circle the width of the line at the end */
static void RoundCap(Fat *f, Boolean first)
{
    FatLine *c = f->curr;
    sword r = (sword) (f->d->GD_gs->GS_scaledLineWid >> 1), rx = r, ry = r;
    Point o = first ? c->origFirst : c->origSecond;

    if (c->kdxHigh != c->kdxLow)
	rx--;
    if (c->kdyHigh != c->kdyLow)
	ry--;
    GrFillEllipseDev(f->d, (sword) (o.P_x - r), (sword) (o.P_y - r),
		     (sword) (o.P_x + rx), (sword) (o.P_y + ry),
		     (word) offsetof(GState, GS_lineAttr));
}

/* MakeEndCap */
static void EndCap(Fat *f, Boolean first)
{
    switch (f->d->GD_gs->GS_lineEnd) {
    case LE_SQUARECAP:
	SquareCap(f, first);
	break;
    case LE_ROUNDCAP:
	RoundCap(f, first);
	ButtCap(f, first);
	break;
    default:
	ButtCap(f, first);
	break;
    }
}

/* CalcLineAngle */
static void LineAngle(FatLine *c)
{
    sdword r;

    if (c->angle != ANGLE_UNSET)
	return;
    r = (sdword) GrSDivWWFixed((WWFixedAsDWord) c->deltaY,
			       (WWFixedAsDWord) c->distance);
    c->angle = (sdword) GrQuickArcSine((WWFixedAsDWord) -r,
				       (word) (c->deltaX >> 16));
    c->sine = (sdword) GrQuickSine((WWFixedAsDWord) c->angle);
    c->cosine = (sdword) GrQuickCosine((WWFixedAsDWord) c->angle);
}

/* CalcCCWAngleDiff: from the previous segment to this one, 0 - 360 */
static sdword CCWAngleDiff(Fat *f)
{
    sdword diff;

    LineAngle(f->prev);
    LineAngle(f->curr);
    diff = (sdword) ((dword) f->curr->angle - (dword) f->prev->angle);
    if ((dword) f->prev->angle > (dword) f->curr->angle)
	diff = (sdword) ((dword) diff + (dword) WWF(360));
    return diff;
}

/* CalcBeveledJoin: the bevel's corner on the outer side */
static void BeveledJoin(Fat *f)
{
    if (f->flags & DFL_FIRST_LINE)
	f->flags |= DFL_CONNECTION_BEVELED;
    if ((sword) (CCWAngleDiff(f) >> 16) <= 180)
	f->curr->FIRST_EXTRA = f->prev->SECOND_B;
    else
	f->curr->FIRST_EXTRA = f->prev->SECOND_A;
}

/* CheckMiterLimit: TRUE when the join is beveled instead */
static Boolean MiterLimit(const Fat *f, sdword angle)
{
    word inv = f->d->GD_gs->GS_inverseMiterLimit;
    sdword s;

    if (inv == 0xffff)
	return TRUE;
    if ((word) (angle >> 16) < 180)
	angle = (sdword) ((dword) WWF(180) - (dword) angle);
    else
	angle = (sdword) ((dword) angle - (dword) WWF(180));
    s = (sdword) GrQuickSine((WWFixedAsDWord) (angle >> 1));
    if (s < 0)
	s = -s;
    if ((s >> 16) == 1)
	return FALSE;
    return (Boolean) ((word) s <= inv);
}

/* CalcMiteredJoin: the sides meet where they cross */
static void MiteredJoin(Fat *f)
{
    FatLine *p = f->prev, *c = f->curr;
    sdword diff = CCWAngleDiff(f), half, hypo;
    sword x, y;

    if (MiterLimit(f, diff)) {
	BeveledJoin(f);
	return;
    }
    half = (sdword) ((dword) diff >> 1);
    hypo = (sdword) GrMulWWFixed(GrQuickSine((WWFixedAsDWord) half),
				 (WWFixedAsDWord) ((dword) (word) Width1(f->d)
						   << 15));
    hypo = (sdword) GrSDivWWFixed((WWFixedAsDWord) hypo,
				  GrQuickCosine((WWFixedAsDWord) half));
    p->joinDeltaY = (sdword) GrMulWWFixed((WWFixedAsDWord) hypo,
					  (WWFixedAsDWord) p->sine);
    p->joinDeltaX = (sdword) GrMulWWFixed((WWFixedAsDWord) hypo,
					  (WWFixedAsDWord) p->cosine);
    c->joinDeltaY = (sdword) GrMulWWFixed((WWFixedAsDWord) hypo,
					  (WWFixedAsDWord) c->sine);
    c->joinDeltaX = (sdword) GrMulWWFixed((WWFixedAsDWord) hypo,
					  (WWFixedAsDWord) c->cosine);
    if ((sword) (half >> 16) <= 90) {		/* on the B side */
	y = Average(WWF(c->FIRST_B.P_y) + c->joinDeltaY,
		    WWF(p->SECOND_B.P_y) - p->joinDeltaY);
	x = Average(WWF(c->FIRST_B.P_x) - c->joinDeltaX,
		    WWF(p->SECOND_B.P_x) + p->joinDeltaX);
	p->SECOND_B.P_x = c->FIRST_B.P_x = x;
	p->SECOND_B.P_y = c->FIRST_B.P_y = y;
	if (f->flags & DFL_FIRST_LINE)
	    f->flags |= DFL_SAVED_FIRST_B;
    } else {					/* on the A side */
	y = Average(WWF(c->FIRST_A.P_y) - c->joinDeltaY,
		    WWF(p->SECOND_A.P_y) + p->joinDeltaY);
	x = Average(WWF(c->FIRST_A.P_x) + c->joinDeltaX,
		    WWF(p->SECOND_A.P_x) - p->joinDeltaX);
	p->SECOND_A.P_x = c->FIRST_A.P_x = x;
	p->SECOND_A.P_y = c->FIRST_A.P_y = y;
	if (f->flags & DFL_FIRST_LINE)
	    f->flags &= (byte) ~DFL_SAVED_FIRST_B;
    }
    p->SECOND_EXTRA.P_x = c->FIRST_EXTRA.P_x = x;
    p->SECOND_EXTRA.P_y = c->FIRST_EXTRA.P_y = y;
    if (f->flags & DFL_FIRST_LINE) {
	f->savedFirst.P_x = x;
	f->savedFirst.P_y = y;
    }
}

/* CalcJoin: the second end is cut square; the first joins the segment
 * before */
static void Join(Fat *f, Boolean first)
{
    ButtCap(f, first);
    if (!first)
	return;
    switch (f->d->GD_gs->GS_lineJoin) {
    case LJ_BEVELED:
	BeveledJoin(f);
	break;
    case LJ_ROUND:
	RoundCap(f, MEC_FIRST);
	break;
    default:
	MiteredJoin(f);
	break;
    }
}

/* DrawFatLineReg: the previous segment's polygon; at the end of a
 * closed mitered polyline its second end is the first join's
 * (HandleMiteredConnection) */
static void DrawReg(Fat *f, int left)
{
    FatLine *p = f->prev;
    sword sep[16];
    int i;

    if (f->d->GD_gs->GS_lineJoin == LJ_MITERED && !left &&
	!(f->flags & DFL_CONNECTION_BEVELED)) {
	if (!(f->flags & DFL_SAVED_FIRST_B))
	    p->SECOND_A = p->SECOND_EXTRA = f->savedFirst;
	else
	    p->SECOND_B = f->savedFirst;
    }
    for (i = 0; i < 6; i++) {
	sep[2 * i] = p->pts[i].P_x;
	sep[2 * i + 1] = p->pts[i].P_y;
    }
    sep[12] = sep[13] = SEPARATOR;
    GrDrawPolygonDev(f->d, sep, 6, ODD_EVEN,
		     (word) offsetof(GState, GS_lineAttr));
}

/* DrawFatPolyLineLow: the segment left points from the end */
static void SegmentLow(Fat *f, int left)
{
    BasicCalcs(f);
    if (!(f->flags & DFL_CONNECTED) && (f->flags & DFL_FIRST_LINE))
	EndCap(f, MEC_FIRST);
    else
	Join(f, MEC_FIRST);
    if (!(f->flags & DFL_CONNECTED) && left == 2)
	EndCap(f, MEC_SECOND);
    else
	Join(f, MEC_SECOND);
    if (!(f->flags & DFL_FIRST_LINE))
	DrawReg(f, left);
    f->flags &= (byte) ~DFL_FIRST_LINE;
}

static void Swap(Fat *f)
{
    FatLine *s = f->prev;

    f->prev = f->curr;
    f->curr = s;
}

/*
 * CheckForSpecialCases, RemovePointRedundancies: no point twice in a
 * row, a closed polyline without its first point again at the end;
 * FALSE when nothing is drawn (fewer than two points, or one point with
 * butt ends).  Returns the points (pts or a copy in *copy).
 */
static Boolean Clean(const GState *gs, const Point *pts, int n,
		     Boolean *connected, Point **copy, const Point **out,
		     int *count)
{
    Point *c;
    int i, k;

    *copy = NULL;
    if (n <= 1)
	return FALSE;
    for (i = 0; i + 1 < n; i++)
	if (pts[i].P_x == pts[i + 1].P_x && pts[i].P_y == pts[i + 1].P_y)
	    break;
    if (i + 1 == n) {			/* nothing twice in a row */
	if (*connected && pts[n - 1].P_x == pts[0].P_x &&
	    pts[n - 1].P_y == pts[0].P_y)
	    n--;
	*out = pts;
	*count = n;
	return TRUE;
    }
    if (n == 2) {
	if (gs->GS_lineEnd == LE_BUTTCAP)
	    return FALSE;
	*connected = FALSE;
	*out = pts;
	*count = 2;
	return TRUE;
    }
    c = malloc(((size_t) n + 1) * sizeof(Point));
    if (!c)
	return FALSE;
    c[0] = pts[0];
    for (i = 1, k = 1; i < n; i++)
	if (pts[i].P_x != c[k - 1].P_x || pts[i].P_y != c[k - 1].P_y)
	    c[k++] = pts[i];
    if (k <= 1) {
	if (gs->GS_lineEnd == LE_BUTTCAP) {
	    free(c);
	    return FALSE;
	}
	c[k++] = c[0];
	*connected = FALSE;
    } else if (*connected && c[k - 1].P_x == c[0].P_x &&
	       c[k - 1].P_y == c[0].P_y)
	k--;
    *copy = c;
    *out = c;
    *count = k;
    return TRUE;
}

/* DoFatLinesLow, DrawFatPolyLine, HandleFatPolyConnection */
static void FatLinesLow(const GrDraw *d, const Point *pts, int n,
			Boolean connected)
{
    Fat f;
    Point *copy;
    int left;

    if (d->GD_w->W_grFlags & WGF_MASK_NULL)
	return;
    memset(&f, 0, sizeof(f));
    if (!Clean(d->GD_gs, pts, n, &connected, &copy, &f.pts, &n))
	return;
    f.d = d;
    f.firstPt = f.firstEnd = 0;
    f.secondEnd = 1;
    f.flags = (byte) (DFL_FIRST_LINE | (connected ? DFL_CONNECTED : 0));
    f.prev = &f.fl[0];
    f.curr = &f.fl[1];
    if (connected) {	/* the closing segment, for the first join */
	f.secondEnd = 0;
	f.firstEnd = n - 1;
	BasicCalcs(&f);
	Join(&f, MEC_SECOND);
	f.firstEnd = 0;
	f.secondEnd = 1;
    }
    left = n;
    for (;;) {
	Swap(&f);
	SegmentLow(&f, left);
	left--;
	if (left > 1)
	    continue;
	if (left == 1 && (f.flags & DFL_CONNECTED)) {
	    f.secondEnd = f.firstPt;
	    continue;
	}
	break;
    }
    Swap(&f);
    DrawReg(&f, left);
    free(copy);
}

/* DoFatLines: each polyline of the separator format */
void GrDrawFatLines(const GrDraw *d, const sword *sep, Boolean connected)
{
    const sword *s = sep, *e;
    Boolean more;
    Point *pts;
    int n, i;

    for (;;) {
	for (e = s, n = 0; *e != SEPARATOR; e += 2, n++)
	    ;
	more = (Boolean) (e[1] != SEPARATOR);
	if (s == sep && more)
	    connected = FALSE;
	pts = malloc(((size_t) n + 1) * sizeof(Point));
	if (!pts)
	    return;
	for (i = 0; i < n; i++) {
	    pts[i].P_x = s[2 * i];
	    pts[i].P_y = s[2 * i + 1];
	}
	FatLinesLow(d, pts, n, connected);
	free(pts);
	if (!more)
	    break;
	s = e + 1;
    }
}
