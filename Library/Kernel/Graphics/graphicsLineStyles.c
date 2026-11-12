/*
 * graphicsLineStyles.c -- dashed lines (after graphicsLineStyles.asm:
 * PolyDashedLineLow, GetLinePoints, GenerateLinePointsX,
 * GenerateLinePointsY, CreateDashArrayOfByteOffsets, ScaleDashValue,
 * ConvertPointsToDashes; graphicsLine.asm: DrawLineLowStyled,
 * ScaleDashLengths, CalcDistance, SqrRootDWordLine).
 *
 * The dash pattern is GS_numOfDashPairs pairs of on and off lengths (a
 * byte each, in GS_dashPairArray), started GS_dashSkipDistance into it.
 *
 * A single thin line goes to the driver as DR_VID_DASH_LINE: the
 * lengths times the line width, in steps along the line's major axis
 * (ScaleDashLengths: times max(|dx|, |dy|) / its length).
 *
 * A polyline is cut into dashes here (PolyDashedLineLow): each segment
 * is stepped pixel by pixel as Bresenham would (GetLinePoints), the
 * pattern (lengths times the width, in pixels) runs on along the
 * polyline, and every dash becomes a polyline of its own in the
 * separator format, with the corners it goes round.  The result is
 * drawn thin or fat like any polyline (graphicsPolyline.c).
 *
 * Unlike the asm, the dashes of every polyline of the format are kept
 * (it began its buffer again at each one) and a skip distance past the
 * pattern's length is taken modulo that length.
 */
#include "graphicsInt.h"
#include <stdlib.h>

static const byte *Pairs(const GState *gs)
{
    return (const byte *) gs->GS_dashPairArray;
}

/* ---------------------------------------------------------------------
 * One thin line: DR_VID_DASH_LINE
 * ------------------------------------------------------------------- */

/* SqrRootDWordLine: the square root of n, WWFixed */
static sdword SqrtLine(dword n)
{
    word r, b, rem;
    dword sq;

    if ((n >> 16) >= 0x1fff) {		/* the asm gave up; 64 bits here */
	dword x = 0, bit = 1UL << 30;
	while (bit > n)
	    bit >>= 2;
	while (bit) {
	    if (n >= x + bit) {
		n -= x + bit;
		x = (x >> 1) + bit;
	    } else
		x >>= 1;
	    bit >>= 2;
	}
	return (sdword) (x << 16);
    }
    r = (word) (n / ((n >> 24) ? 8192 : 300) + 2);
    for (;;) {
	b = r;
	r = (word) ((n / b + b) >> 1);
	if (r == b || (word) (b - r) == 1 || (word) (b - r) == 0xffff)
	    break;
    }
    for (;;) {
	sq = (dword) r * r;
	if (sq > n && ((sq - n) >> 16) > 0) {	/* aWeirdCase */
	    r--;
	    continue;
	}
	break;
    }
    rem = (word) (n - sq);
    return (sdword) (((dword) r << 16) |
		     (word) GrUDivWWFixed((dword) rem << 16, (dword) r << 17));
}

/* ScaleDashLengths, CalcDistance */
static void ScaleDashLengths(const GrDraw *d, const DashInfo *di,
			     WWFixed *patt)
{
    const GState *gs = d->GD_gs;
    word ax = (word) (di->DI_pt1.P_x - di->DI_pt2.P_x);
    word bx = (word) (di->DI_pt1.P_y - di->DI_pt2.P_y);
    sdword ratio, mul;
    word i, s;

    if ((sword) ax < 0)
	ax = (word) -ax;
    if ((sword) bx < 0)
	bx = (word) -bx;
    if (ax < bx) {
	s = ax; ax = bx; bx = s;
    }
    ratio = (sdword) GrSDivWWFixed((dword) ax << 16,
				   (WWFixedAsDWord) SqrtLine((dword) ax * ax +
							     (dword) bx * bx));
    s = GrScaleScalar(d, 256L << 16);	/* the width's scale, 8.8 */
    mul = (sdword) GrMulWWFixed((WWFixedAsDWord) ratio, (dword) s << 8);
    for (i = 0; i < 2 * gs->GS_numOfDashPairs; i++) {
	sdword v = (sdword) GrMulWWFixed((WWFixedAsDWord) mul,
					 (dword) Pairs(gs)[i] << 16);
	if (!v)
	    v = 1L << 16;
	patt[i].WWF_int = (sword) (v >> 16);
	patt[i].WWF_frac = (word) v;
    }
}

/* DrawLineLowStyled: device coordinates */
void GrDrawLineLowStyled(const GrDraw *d, sword x1, sword y1, sword x2,
			 sword y2, word attr)
{
    const GState *gs = d->GD_gs;
    WWFixed patt[MAX_DASH_ARRAY_PAIRS * 2];
    DashInfo di;
    DriverRegs r;

    GrAdjustLineEnds(gs, &x1, &y1, &x2, &y2);
    if (GrRejectRect(d, x1, y1, x2, y2))
	return;
    di.DI_pt1.P_x = x1;
    di.DI_pt1.P_y = y1;
    di.DI_pt2.P_x = x2;
    di.DI_pt2.P_y = y2;
    di.DI_nPairs = gs->GS_numOfDashPairs;
    di.DI_pattIdx = gs->GS_dashSkipDistance;
    ScaleDashLengths(d, &di, patt);
    di.DI_patt = patt;
    memset(&r, 0, sizeof(r));
    r.DR_farPtr = &di;
    GrCallDriver(d, DR_VID_DASH_LINE, &r, attr);
}

/* ---------------------------------------------------------------------
 * Polylines: dashes as polylines
 * ------------------------------------------------------------------- */

typedef struct {
    sword	*buf;
    size_t	len, size;		/* in words */
    Boolean	full;
} Out;

static void Put(Out *o, sword v)
{
    if (o->len == o->size) {
	sword *n = realloc(o->buf, 2 * o->size * sizeof(sword));
	if (!n) {
	    o->full = TRUE;
	    return;
	}
	o->buf = n;
	o->size *= 2;
    }
    o->buf[o->len++] = v;
}

static void PutPoint(Out *o, const sword *p)
{
    Put(o, p[0]);
    Put(o, p[1]);
}

/* GetLinePoints: the pixels from x1, y1 to x2, y2 as Bresenham steps
 * them from the end with the smaller major coordinate; *n of them (two
 * for a point) */
static sword *LinePoints(sword x1, sword y1, sword x2, sword y2, int *n)
{
    int dx = x2 - x1, dy = y2 - y1, adx, ady, k, i, d, incr1, incr2, bump;
    Boolean yMajor, reverse;
    sword *p, a, b, ea, eb;

    adx = (word) (dx < 0 ? -dx : dx);
    ady = (word) (dy < 0 ? -dy : dy);
    k = (adx > ady ? adx : ady);
    *n = k ? k + 1 : 2;
    p = malloc((size_t) *n * 2 * sizeof(sword));
    if (!p)
	return NULL;
    if (!k) {
	p[0] = p[2] = x1;
	p[1] = p[3] = y1;
	return p;
    }
    yMajor = (Boolean) (adx <= ady);
    /* a: the major coordinate, b: the minor */
    a = yMajor ? y1 : x1;
    b = yMajor ? x1 : y1;
    ea = yMajor ? y2 : x2;
    eb = yMajor ? x2 : y2;
    reverse = (Boolean) (a > ea);
    if (reverse) {
	sword s;
	s = a; a = ea; ea = s;
	s = b; b = eb; eb = s;
    }
    incr1 = 2 * (yMajor ? adx : ady);
    d = incr1 - k;
    incr2 = d - k;
    bump = b > eb ? -1 : 1;
    for (i = 0;; i++) {
	int at = reverse ? k - i : i;
	p[2 * at + (yMajor ? 1 : 0)] = a;
	p[2 * at + (yMajor ? 0 : 1)] = b;
	if (a == ea)
	    break;
	if ((sword) d > 0) {
	    d = (sword) (d + incr2);
	    b = (sword) (b + bump);
	} else
	    d = (sword) (d + incr1);
	a++;
    }
    return p;
}

typedef struct {
    int		nElems;
    sword	elems[MAX_DASH_ARRAY_PAIRS * 2];	/* in pixels */
    Boolean	on;		/* DIS_currentStatus */
    long	next;		/* DIS_offsetTo1stPtNextStatus */
    int		elem;		/* DIS_offsetToNextArrayElement */
} Dashes;

/* ConvertPointsToDashes: the pixels pts[0..n-1] of a segment; first:
 * the polyline's first segment */
static void Convert(Dashes *ds, const sword *pts, int n, Boolean first,
		    long skip, Out *o)
{
    long si, last = n - 1, ax;
    int bx;

    if (first) {				/* LINE_INITIAL */
	bx = 0;
	ax = ds->elems[0] + 2 - skip;
	if (ax > 0) {			/* in the first dash */
	    bx = 1;
	    PutPoint(o, &pts[0]);
	    si = 1;
	    ds->on = TRUE;
	    ax -= 2;
	} else {
	    si = 0;
	    ds->on = FALSE;
	    ax += ds->elems[1];
	    bx = ds->nElems > 2 ? 2 : 0;
	}
    } else {
	bx = ds->elem;
	ax = ds->next;
	si = 0;
    }
    si += ax;
    for (;;) {
	if (!ds->on) {
	    if (si > last)
		break;
	    PutPoint(o, &pts[2 * si]);		/* a dash starts */
	    si++;
	    ds->on = TRUE;
	    si += ds->elems[bx];
	    if (++bx >= ds->nElems)
		bx = 0;
	}
	if (si > last) {			/* on round the corner */
	    PutPoint(o, &pts[2 * last]);
	    break;
	}
	PutPoint(o, &pts[2 * si]);		/* it ends */
	Put(o, SEPARATOR);
	si++;
	ds->on = FALSE;
	si += ds->elems[bx];
	if (++bx >= ds->nElems)
	    bx = 0;
    }
    ds->next = si - last;
    ds->elem = bx;
}

/*
 * PolyDashedLineLow: the dashes of the polylines (device coordinates,
 * the separator format) in the separator format, in the C heap; NULL
 * when there are none.
 */
sword *GrDashPolyline(const GrDraw *d, const sword *sep, word count)
{
    const GState *gs = d->GD_gs;
    word w = gs->GS_scaledLineWid, total = 0;
    Dashes ds;
    Out o;
    const sword *s = sep;
    long skip;
    int i, n;
    Boolean any = FALSE;

    (void) count;
    memset(&ds, 0, sizeof(ds));
    ds.nElems = 2 * gs->GS_numOfDashPairs;
    if (!ds.nElems)
	return NULL;
    for (i = 0; i < ds.nElems; i++) {	/* CreateDashArrayOfByteOffsets */
	word v = (word) (Pairs(gs)[i] * w);
	if (!v)
	    v = 1;
	total = (word) (total + v);
	ds.elems[i] = (sword) (i & 1 ? v : v - 2);
    }
    skip = (long) (word) (gs->GS_dashSkipDistance * w);
    if (total)
	skip %= total;
    o.size = 256;
    o.len = 0;
    o.full = FALSE;
    o.buf = malloc(o.size * sizeof(sword));
    if (!o.buf)
	return NULL;
    for (;;) {				/* each polyline */
	Boolean first = TRUE;
	size_t start = o.len;

	for (; s[2] != SEPARATOR; s += 2) {	/* each segment */
	    sword *pts = LinePoints(s[0], s[1], s[2], s[3], &n);
	    if (!pts)
		break;
	    Convert(&ds, pts, n, first, skip, &o);
	    first = FALSE;
	    free(pts);
	}
	if (!first && ds.on)
	    Put(&o, SEPARATOR);		/* the last dash ends */
	if (o.len > start)
	    any = TRUE;
	s += 2;				/* at the separator */
	if (s[1] == SEPARATOR)
	    break;
	s++;
    }
    Put(&o, SEPARATOR);
    Put(&o, SEPARATOR);
    if (!any || o.full) {
	free(o.buf);
	return NULL;
    }
    return o.buf;
}
