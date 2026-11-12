/*
 * graphicsCalcConic.c -- the conic section scan converter (after
 * graphicsCalcConic.asm: CalcConic, CalcOctant, GetOctant, InitOctant1-8,
 * EvaluateOctant, DrawEvenOctant, DrawOddOctant, FixupInOct1-8,
 * AddPoint, CalcDifferential).
 *
 * The points along S(x,y) = Ax^2 + Bxy + Cy^2 + Dx + Ey + F = 0, from
 * C_beg (at the origin of the equation) round to C_end, octant by
 * octant, with a decision variable d and its differences u and v (the
 * midpoint algorithm, Foley & van Dam).  All in doubled device
 * coordinates; AddPoint halves them, rounding towards the center.
 *
 * The coefficients are 64 bits (HugeInt).  Where the asm halved or
 * quartered one it kept the bits shifted out in cl and ch and added
 * their carries; that is done here bit for bit, so the pixels are the
 * same.  CALC_CONIC_START_ANYWHERE (unused code in the asm) is left out.
 *
 * The result is one polyline in the separator format (no separators
 * inside: a stretch outside C_winTop..C_winBot is cut short and the
 * polyline goes on where the curve comes back), consecutive points in
 * one direction merged, in the C heap.
 */
#include "graphicsInt.h"
#include <stdlib.h>

#define END_POINT_DELTA		2
#define DIFFERENTIAL_MAXIMUM	3
#define EOR			((sword) EOREGREC)

typedef struct {
    GrConic	*c;
    int64_t	d, u, v, k1, k2, k3;	/* CF_d .. CF_k3 */
    int		octCount, octant;
    sword	cx, cy;			/* CF_curr */
    sword	lx, ly;			/* CF_last */
    sword	ox, oy;			/* CF_orig */
    sword	dirx, diry;		/* CF_dir */
    sword	sqx, sqy, dgx, dgy;	/* CF_dxsquare .. CF_dydiag */
    word	arcDiff;		/* CF_arcDiff */
    sword	alx, aly;		/* CF_arcLast */
    sword	*buf;
    size_t	len, size;		/* words */
    word	points;			/* CF_bufPoints */
    Boolean	full;
} Conic;

static const sword sqreDeltaX[] = { 1,  0,  0, -1, -1,  0,  0,  1 };
static const sword sqreDeltaY[] = { 0,  1,  1,  0,  0, -1, -1,  0 };
static const sword diagDeltaX[] = { 1,  1, -1, -1, -1, -1,  1,  1 };
static const sword diagDeltaY[] = { 1,  1,  1,  1, -1, -1, -1, -1 };

/* sarqw: the bit shifted out */
static int Sar(int64_t *v)
{
    int b = (int) (*v & 1);

    *v >>= 1;
    return b;
}

static void Put(Conic *f, sword x, sword y)
{
    if (f->len + 2 > f->size) {
	sword *n = realloc(f->buf, 2 * f->size * sizeof(sword));
	if (!n) {
	    f->full = TRUE;
	    return;
	}
	f->buf = n;
	f->size *= 2;
    }
    f->buf[f->len++] = x;
    f->buf[f->len++] = y;
}

/* ---------------------------------------------------------------------
 * Setting up an octant
 * ------------------------------------------------------------------- */

/* GetOctant: the octant the gradient (dx, dy) points into */
static int GetOctant(int64_t dx, int64_t dy)
{
    if (dx >= 0) {
	if (dy >= 0)
	    return dx >= dy ? 7 : 8;
	return dx <= -dy ? 5 : 6;
    }
    if (dy >= 0)
	return -dx <= dy ? 1 : 2;
    return -dx >= -dy ? 3 : 4;
}

/* EvaluateOctant: d gets w/4 (the bits shifted out carried as the asm
 * did, cl holding a half from InitOctant4 and 5) and F; then the moves
 * and the second differences for the octant */
static void Evaluate(Conic *f, int64_t w, byte cl)
{
    const GrConic *c = f->c;
    byte ch;
    int b0, b1, i = f->octant - 1, cf;
    word s;
    int64_t x, z;

    b0 = Sar(&w);
    b1 = Sar(&w);
    ch = (byte) ((b1 << 7) | (b0 << 6));
    s = (word) (cl + ch);
    cf = s >> 8;
    cl = (byte) s;
    w += f->d + cf;
    w += c->C_F + (cl >> 7);
    f->d = w;

    f->sqx = sqreDeltaX[i];
    f->sqy = sqreDeltaY[i];
    f->dgx = diagDeltaX[i];
    f->dgy = diagDeltaY[i];
    /* k1Table: A C C A A C C A; k2Table: + + - - + + - -; k3Table:
     * C A A C C A A C */
    x = (i == 0 || i == 3 || i == 4 || i == 7) ? c->C_A : c->C_C;
    z = (i == 0 || i == 3 || i == 4 || i == 7) ? c->C_C : c->C_A;
    f->k1 = 2 * x;
    f->k2 = f->k1 + ((i & 2) ? -c->C_B : c->C_B);
    f->k3 = f->k2 + ((i & 2) ? -c->C_B : c->C_B) + 2 * z;
}

/* InitOctant1-8: d, u and v at the start, for the octant */
static void InitOctant(Conic *f)
{
    const GrConic *c = f->c;
    int64_t t;
    byte cl = 0;
    int b, b2;

    switch (f->octant) {
    case 1:
    case 2: {
	int64_t x = f->octant == 1 ? c->C_A : c->C_C;
	int64_t y = f->octant == 1 ? c->C_D : c->C_E;
	int64_t z = f->octant == 1 ? c->C_E : c->C_D;
	f->u = f->d = x + y;
	t = c->C_B;
	b = Sar(&t);
	f->d += t;
	f->u = t + f->u + b;
	f->v = f->u + z;
	t = z;
	b2 = Sar(&t);
	f->d += t + (b & b2);
	Evaluate(f, f->octant == 1 ? c->C_C : c->C_A, 0);
	break;
    }
    case 3:
	f->u = f->d = c->C_C + c->C_E;
	t = c->C_B;
	b = Sar(&t);
	f->d -= t;
	f->u = f->u - t - b;
	f->v = f->u - c->C_D;
	t = c->C_D;
	b2 = Sar(&t);
	f->d -= t + (b & b2);
	Evaluate(f, c->C_A, 0);
	break;
    case 4:
	f->u = f->d = c->C_A - c->C_D;
	t = c->C_B;
	b = Sar(&t);
	cl = (byte) (b << 7);
	f->d -= t;
	f->u = f->u - t - b;
	f->v = f->u + c->C_E;
	t = c->C_E;
	b2 = Sar(&t);
	f->d += t + b2;
	Evaluate(f, c->C_C, cl);
	break;
    case 5:
	f->u = f->d = c->C_A - c->C_D;
	t = c->C_B;
	b = Sar(&t);
	cl = (byte) (b << 7);
	f->d += t;
	f->u = t + f->u + b;
	f->v = f->u - c->C_E;
	t = c->C_E;
	b2 = Sar(&t);
	f->d -= t + b2;
	Evaluate(f, c->C_C, cl);
	break;
    case 6:
	f->u = c->C_C - c->C_E;
	t = c->C_B;
	b = Sar(&t);
	f->u = f->d = t + f->u + b;
	f->v = f->u - c->C_D;
	t = c->C_D;
	b2 = Sar(&t);
	f->d -= t + b2;
	Evaluate(f, c->C_A, 0);
	break;
    case 7:
	f->u = c->C_C - c->C_E;
	t = c->C_B;
	b = Sar(&t);
	f->u = f->d = f->u - t - b;
	f->v = f->u + c->C_D;
	t = c->C_D;
	b2 = Sar(&t);
	f->d += t + b2;
	Evaluate(f, c->C_A, 0);
	break;
    default:
	f->u = c->C_A + c->C_D;
	t = c->C_B;
	b = Sar(&t);
	f->u = f->d = f->u - t - b;
	f->v = f->u - c->C_E;
	t = c->C_E;
	b2 = Sar(&t);
	f->d -= t + b2;
	Evaluate(f, c->C_C, 0);
	break;
    }
}

/* ---------------------------------------------------------------------
 * The points
 * ------------------------------------------------------------------- */

/* CalcDifferential: how far x, y is from p, at most 3 */
static word Differential(sword x, sword y, const Point *p)
{
    word dx = (word) (x - p->P_x), dy = (word) (y - p->P_y);

    if ((sword) dx < 0)
	dx = (word) -dx;
    if ((sword) dy < 0)
	dy = (word) -dy;
    dx = (word) (dx + dy);
    return (sword) dx >= DIFFERENTIAL_MAXIMUM ? DIFFERENTIAL_MAXIMUM : dx;
}

static Boolean InWin(const GrConic *c, sword y)
{
    return (Boolean) (y >= c->C_winTop && y <= c->C_winBot);
}

/* AddPoint: CF_curr (halved) to the buffer; terminate: the last one */
static void AddPoint(Conic *f, Boolean terminate)
{
    GrConic *c = f->c;
    sword x = f->cx, y = f->cy;
    word diff;

    if (x < c->C_center.P_x)
	x++;
    x >>= 1;
    if (y < c->C_center.P_y)
	y++;
    y >>= 1;

    if (terminate) {
	if (c->C_conicType != GR_CT_ELLIPSE) {
	    x = c->C_conicEnd.P_x;
	    y = c->C_conicEnd.P_y;
	}
	if ((x != f->lx || y != f->ly) && f->lx != EOR && InWin(c, f->ly)) {
	    Put(f, f->lx, f->ly);
	    f->points++;
	}
	if (f->cx != EOR && InWin(c, y)) {
	    Put(f, x, y);
	    f->points++;
	}
	Put(f, EOR, EOR);
	return;
    }
    if (x == f->lx && y == f->ly)
	return;

    if (c->C_conicType != GR_CT_ELLIPSE) {	/* checkArcFlags */
	if (c->C_conicInfo == GR_CAI_FIND_START) {
	    diff = Differential(x, y, &c->C_conicBeg);
	    if (diff) {
		if (diff <= f->arcDiff) {	/* getting closer */
		    f->arcDiff = diff;
		    f->alx = x;
		    f->aly = y;
		    return;
		}
		Put(f, c->C_conicBeg.P_x, c->C_conicBeg.P_y);
		Put(f, f->alx, f->aly);
		f->points += 2;
	    }
	    f->arcDiff = EOREGREC;
	    c->C_conicInfo = GR_CAI_FIND_END;
	} else if (c->C_conicInfo == GR_CAI_FIND_END) {
	    diff = Differential(x, y, &c->C_conicEnd);
	    if (!diff || diff > f->arcDiff) {
		f->octCount = 1;		/* this octant is the last */
		c->C_conicInfo = GR_CAI_DONE;
		return;
	    }
	    f->arcDiff = diff;
	} else
	    return;
    }

    if (!InWin(c, y)) {				/* outOfBounds */
	if (f->lx == EOR)
	    return;
	if (f->ox != EOR) {
	    Put(f, f->lx, f->ly);
	    f->points++;
	    f->ox = EOR;
	}
	f->lx = EOR;
	return;
    }
    if (f->lx == EOR) {
	Put(f, x, y);
	f->points++;
    } else if (f->ox == EOR || (sword) (x - f->lx) != f->dirx ||
	       (sword) (y - f->ly) != f->diry) {
	if (f->ox != EOR) {			/* newDirection */
	    Put(f, f->lx, f->ly);
	    f->points++;
	}
	f->ox = f->lx;				/* setNewDir */
	f->oy = f->ly;
	f->dirx = (sword) (x - f->lx);
	f->diry = (sword) (y - f->ly);
    }
    f->lx = x;
    f->ly = y;
}

static Boolean AtEnd(const Conic *f)
{
    return (Boolean) (f->cx == f->c->C_end.P_x && f->cy == f->c->C_end.P_y);
}

static Boolean InBounds(const Conic *f)
{
    const Rectangle *b = &f->c->C_bounds;

    return (Boolean) (f->cx >= b->R_left && f->cx <= b->R_right &&
		      f->cy >= b->R_top && f->cy <= b->R_bottom);
}

/* FixupInOct1-8: back from a diagonal move that crossed over */
static void Fixup(Conic *f)
{
    switch (f->octant) {
    case 1: f->cx--; break;
    case 2: f->cx--; f->cy--; break;
    case 3: f->cy--; break;
    case 4: f->cx++; f->cy--; break;
    case 5: f->cx++; break;
    case 6: f->cx++; f->cy++; break;
    case 7: f->cy++; break;
    default: f->cx--; f->cy++; break;
    }
}

/* DrawEvenOctant */
static void DrawEvenOctant(Conic *f)
{
    int64_t half = f->k2, t;
    byte cl, ch;
    word s;
    int b, cf;

    if (Sar(&half))
	half++;
    while (half >= f->v && InBounds(f)) {
	AddPoint(f, FALSE);
	if (f->d >= 0) {			/* diagonal */
	    f->cx = (sword) (f->cx + f->dgx);
	    f->cy = (sword) (f->cy + f->dgy);
	    f->u += f->k2;
	    f->v += f->k3;
	    if (f->v > f->u) {			/* crossed over */
		Fixup(f);
		f->u -= f->k2;
		f->v -= f->k3;
		goto square;
	    }
	    f->d += f->v;
	} else {
	square:
	    f->cx = (sword) (f->cx + f->sqx);
	    f->cy = (sword) (f->cy + f->sqy);
	    f->v += f->k2;
	    f->u += f->k1;
	    f->d += f->u;
	}
	if (AtEnd(f))
	    break;
    }

    /* on to the next octant: d, u, v at the midpoint of the other
     * move, the halves and eighths carried as the asm did (it read
     * k2/2 with its two high words swapped, the same for |k2| < 2^31) */
    t = -f->u;
    f->d += t;
    f->u = t + f->v;
    t = f->v;
    b = Sar(&t);
    cl = (byte) (b << 7);
    f->d += t;
    t = 3 * f->k3;
    ch = 0;
    b = Sar(&t); ch = (byte) ((ch >> 1) | (b << 7));
    b = Sar(&t); ch = (byte) ((ch >> 1) | (b << 7));
    b = Sar(&t); ch = (byte) ((ch >> 1) | (b << 7));
    s = (word) (cl + ch);
    cf = s >> 8;
    cl = (byte) s;
    f->d += t + cf;
    t = f->k2;
    b = Sar(&t);
    ch = (byte) (b << 7);
    s = (word) (cl + ch);
    cf = s >> 8;
    f->d -= t + cf;
    f->u -= t;
    t = f->k3;
    b = Sar(&t);
    cl = (byte) (b << 7);
    s = (word) (cl + ch);
    cf = s >> 8;
    f->u += t + cf;
    f->v = t + f->v + b - f->k2;
    t = f->k2;
    f->k1 += f->k3 - 2 * t;
    f->k2 = f->k3 - t;
    {
	sword o = f->sqx;
	f->sqx = f->sqy;
	f->sqy = (sword) -o;
    }
}

/* DrawOddOctant */
static void DrawOddOctant(Conic *f)
{
    int64_t half = f->k2, t;

    if (Sar(&half))
	half++;
    while (half > f->u) {
	AddPoint(f, FALSE);
	if (f->d >= 0) {			/* square */
	    f->cx = (sword) (f->cx + f->sqx);
	    f->cy = (sword) (f->cy + f->sqy);
	    f->v += f->k2;
	    f->u += f->k1;
	    if (half + f->v - f->u < f->u) {	/* crossed over */
		Fixup(f);
		f->v -= f->k2;
		f->u -= f->k1;
		goto diag;
	    }
	    f->d += f->u;
	} else {
	diag:
	    f->cx = (sword) (f->cx + f->dgx);
	    f->cy = (sword) (f->cy + f->dgy);
	    f->u += f->k2;
	    f->v += f->k3;
	    f->d += f->v;
	}
	if (AtEnd(f))
	    break;
    }

    t = f->u;
    f->d += t;
    t <<= 1;
    {
	int64_t ov = f->v;
	f->v = t - ov;
	f->d -= ov;
    }
    t = f->k1 - f->k2;
    f->d += t;
    f->v += t;
    f->u += t;
    t <<= 1;
    f->k2 += t;
    t <<= 1;
    f->k3 += t;
    {
	sword o = f->dgx;
	f->dgx = f->dgy;
	f->dgy = (sword) -o;
    }
}

static void DrawOctant(Conic *f)
{
    if (f->octant & 1)
	DrawOddOctant(f);
    else
	DrawEvenOctant(f);
}

/*
 * CalcConic: the points from C_beg round to C_end (a full turn for an
 * ellipse, two for an arc, which AddPoint cuts to the part between
 * C_conicBeg and C_conicEnd); *count of them, then EOREGREC twice.
 * NULL when out of memory.
 */
sword *GrCalcConic(GrConic *c, word *count)
{
    Conic f;
    int dx, dy;

    memset(&f, 0, sizeof(f));
    f.c = c;
    f.size = 256;
    f.buf = malloc(f.size * sizeof(sword));
    if (!f.buf)
	return NULL;
    f.octant = GetOctant(c->C_D, c->C_E);	/* CalcOctant: the gradient */
    f.octCount = c->C_conicType != GR_CT_ELLIPSE ? 16 : 8;
    InitOctant(&f);
    f.arcDiff = EOREGREC;
    f.ox = f.lx = f.oy = f.ly = EOR;
    f.cx = c->C_beg.P_x;
    f.cy = c->C_beg.P_y;

    do {
	DrawOctant(&f);
	if (--f.octant == 0)
	    f.octant = 8;
    } while (--f.octCount);
    dx = f.cx - c->C_end.P_x;
    dy = f.cy - c->C_end.P_y;
    if ((word) (dx < 0 ? -dx : dx) > END_POINT_DELTA ||
	(word) (dy < 0 ? -dy : dy) > END_POINT_DELTA)
	DrawOctant(&f);
    f.cx = c->C_end.P_x;
    f.cy = c->C_end.P_y;
    AddPoint(&f, TRUE);
    if (f.full) {
	free(f.buf);
	return NULL;
    }
    *count = f.points;
    return f.buf;
}
