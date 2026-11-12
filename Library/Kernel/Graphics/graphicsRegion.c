/*
 * graphicsRegion.c -- region arithmetic (after graphicsRegion.asm:
 * GrChunkRegOp, GrPtrRegOp, GrNOTPtrReg, GrMoveReg, GrTestPointInReg,
 * GrTestRectInReg, GrGetPtrRegBounds).
 *
 * The regions have no bounds words (CInclude/Internal/grWinInt.h).
 * GrDrawRegion, GrSetClipRect and the rest come with the GState.
 *
 * The asm had a routine for each of AND, OR and NOT; here one walk does
 * all three: it steps down the bands of both regions at once and
 * combines the pairs of a band.  NOT is the whole coordinate space
 * without the region.  Equal bands one after the other become one
 * (ElimRedundant), touching pairs one, an empty last band is left out;
 * so the result may be shorter than the asm's, for the same area.
 */
#include "kernelC.h"
#include <Internal/grWinInt.h>
#include <lmem.h>
#include <string.h>

#define INF	0x7fffffffL

/* the coordinate space NOT works in (GrNOTPtrReg) */
static const word everything[] = {
    (word) (MIN_COORD - 1), EOREGREC,
    (word) (MAX_COORD - 1), (word) MIN_COORD, (word) (MAX_COORD - 1),
    EOREGREC,
    EOREGREC
};

/* a band of a region */
typedef struct {
    const word	*p;		/* its bottom word */
    sdword	bottom;		/* INF past the last one */
    const word	*pairs;		/* its pairs, NULL past the last one */
} Band;

static void BandSet(Band *b, const word *p)
{
    b->p = p;
    if (*p == EOREGREC) {
	b->bottom = INF;
	b->pairs = NULL;
    } else {
	b->bottom = (sword) *p;
	b->pairs = p + 1;
    }
}

static void BandNext(Band *b)
{
    const word *p = b->pairs;

    while (*p != EOREGREC)
	p++;
    BandSet(b, p + 1);
}

/* the output: counted on past the room, for the estimate */
typedef struct {
    word	*out;
    dword	n, room;	/* in words */
} Out;

static void Put(Out *o, word v)
{
    if (o->n < o->room)
	o->out[o->n] = v;
    o->n++;
}

/* the next on (not in) or off + 1 (in) of a line */
static sdword Edge(const word *pairs, Boolean in)
{
    if (!pairs || *pairs == EOREGREC)
	return INF;
    return in ? (sdword) (sword) pairs[1] + 1 : (sdword) (sword) pairs[0];
}

static Boolean Combine(RegionOpFlags op, Boolean a, Boolean b)
{
    if (op & ROF_AND_OP)
	return a && b;
    if (op & ROF_OR_OP)
	return a || b;
    return a && !b;			/* NOT: everything without reg1 */
}

/* the pairs of a band of the result (GrANDLine, GrORLine) */
static void CombineLine(RegionOpFlags op, const word *a, const word *b,
			Out *o)
{
    Boolean inA = FALSE, inB = FALSE, in = FALSE, now;
    sdword x, ea, eb;

    for (;;) {
	ea = Edge(a, inA);
	eb = Edge(b, inB);
	x = ea < eb ? ea : eb;
	if (x == INF)
	    break;
	while ((ea = Edge(a, inA)) == x) {	/* all edges here */
	    if (inA)
		a += 2;
	    inA = (Boolean) !inA;
	}
	while ((eb = Edge(b, inB)) == x) {
	    if (inB)
		b += 2;
	    inB = (Boolean) !inB;
	}
	now = Combine(op, inA, inB);
	if (now != in) {
	    Put(o, (word) (now ? x : x - 1));
	    in = now;
	}
    }
    if (in)				/* not reached: the edges pair up */
	Put(o, (word) (MAX_COORD - 1));
    Put(o, EOREGREC);
}

Boolean GrPtrRegOp(RegionOpFlags op, const Region *reg1, const Region *reg2,
		   Region *reg3, word *size)
{
    Band a, b;
    Out o;
    dword start, prev = 0;
    Boolean havePrev = FALSE, prevEmpty = FALSE;
    sdword bottom, y;

    if (op & ROF_NOT_OP) {
	reg2 = reg1;
	reg1 = everything;
    }
    o.out = reg3;
    o.n = 0;
    o.room = *size / 2;
    BandSet(&a, reg1);
    BandSet(&b, reg2);
    for (y = -INF;;) {
	while (a.bottom < y)
	    BandNext(&a);
	while (b.bottom < y)
	    BandNext(&b);
	bottom = a.bottom < b.bottom ? a.bottom : b.bottom;
	if (bottom == INF)		/* both ended: empty from here */
	    break;
	start = o.n;
	Put(&o, (word) bottom);
	CombineLine(op, a.pairs, b.pairs, &o);
	if (havePrev && o.n <= o.room && o.n - start == start - prev &&
	    !memcmp(reg3 + prev + 1, reg3 + start + 1,
		    (start - prev - 1) * sizeof(word))) {
	    reg3[prev] = (word) bottom;	/* the same band, longer */
	    o.n = start;
	} else {
	    havePrev = TRUE;
	    prev = start;
	    prevEmpty = (Boolean) (o.n - start == 2);
	}
	y = bottom + 1;
    }
    if (havePrev && prevEmpty)
	o.n = prev;
    if (o.n <= 2)			/* nothing but an empty band */
	o.n = 0;
    Put(&o, EOREGREC);
    if (o.n > o.room) {
	dword want = o.n * 2 + o.n / 2 + 14;
	*size = (word) (want > 0xfff0 ? 0xfff0 : want);
	return TRUE;
    }
    *size = (word) (o.n * 2);
    return FALSE;
}

void GrChunkRegOp(RegionOpFlags op, optr reg1, optr reg2, optr reg3)
{
    word size1 = LMemGetChunkSize(reg1), size2, size, got;

    /* the asm's first guess */
    if (op & ROF_NOT_OP)
	size = (word) (2 * size1 + 14);
    else {
	size2 = LMemGetChunkSize(reg2);
	size = (word) (op & ROF_AND_OP ? (size1 > size2 ? size1 : size2) :
			size1 + size2);
	size = (word) (size + size / 2 + 14);
    }
    for (;;) {
	if (LMemGetChunkSize(reg3) < size)
	    LMemReAlloc(reg3, size);
	got = LMemGetChunkSize(reg3);
	if (!GrPtrRegOp(op, LMemDeref(reg1),
			(op & ROF_NOT_OP) ? NULL : LMemDeref(reg2),
			LMemDeref(reg3), &got))
	    break;
	size = got;
    }
    LMemReAlloc(reg3, got);
}

void GrMoveReg(Region *reg, sword xOffset, sword yOffset)
{
    word *p = reg;

    while (*p != EOREGREC) {
	*p = (word) (*p + yOffset);
	for (p++; *p != EOREGREC; p++)
	    *p = (word) (*p + xOffset);
	p++;
    }
}

word GrGetPtrRegBounds(const Region *reg, Rectangle *bounds)
{
    const word *p = reg;
    sword left = 0x3fff, right = (sword) 0xc000;

    if (*p == EOREGREC) {
	bounds->R_left = bounds->R_top = bounds->R_right =
	    bounds->R_bottom = (sword) EOREGREC;
	return sizeof(word);
    }
    bounds->R_top = (sword) (*p + 1);
    bounds->R_bottom = bounds->R_top;
    while (*p != EOREGREC) {
	bounds->R_bottom = (sword) *p;
	for (p++; *p != EOREGREC; p += 2) {
	    if ((sword) p[0] < left)
		left = (sword) p[0];
	    if ((sword) p[1] > right)
		right = (sword) p[1];
	}
	p++;
    }
    bounds->R_left = left;
    bounds->R_right = right;
    return (word) ((p + 1 - reg) * sizeof(word));
}

/* in the band at p: the pair holding x */
static const word *PairAt(const word *pairs, sword x)
{
    for (; *pairs != EOREGREC; pairs += 2)
	if (x >= (sword) pairs[0] && x <= (sword) pairs[1])
	    return pairs;
    return NULL;
}

Boolean GrTestPointInReg(const Region *reg, word xPos, word yPos,
			 Rectangle *boundingRect)
{
    const word *p = reg, *pair;
    sword top = (sword) 0xc000, x = (sword) xPos, y = (sword) yPos;

    while (*p != EOREGREC) {
	if (y <= (sword) *p) {
	    pair = PairAt(p + 1, x);
	    if (!pair)
		return FALSE;
	    if (boundingRect) {
		boundingRect->R_left = (sword) pair[0];
		boundingRect->R_right = (sword) pair[1];
		boundingRect->R_top = top;
		boundingRect->R_bottom = (sword) *p;
	    }
	    return TRUE;
	}
	top = (sword) (*p + 1);
	for (p++; *p != EOREGREC; p++)
	    ;
	p++;
    }
    return FALSE;
}

/* how much of left..right a band covers: TRRT_* */
static TestRectReturnType LineCover(const word *pairs, sword left,
				    sword right)
{
    for (; *pairs != EOREGREC; pairs += 2) {
	if ((sword) pairs[0] > right)
	    break;
	if ((sword) pairs[1] < left)
	    continue;
	return (sword) pairs[0] <= left && (sword) pairs[1] >= right ?
	    TRRT_IN : TRRT_PARTIAL;
    }
    return TRRT_OUT;
}

TestRectReturnType GrTestRectInReg(const Region *reg, sword left, sword top,
				   sword right, sword bottom)
{
    const word *p = reg;
    TestRectReturnType so = TRRT_OUT, line;
    Boolean first = TRUE;

    while (*p != EOREGREC) {
	if ((sword) *p >= top) {
	    line = LineCover(p + 1, left, right);
	    if (line == TRRT_PARTIAL || (!first && line != so))
		return TRRT_PARTIAL;
	    so = line;
	    first = FALSE;
	    if ((sword) *p >= bottom)
		return so;
	}
	for (p++; *p != EOREGREC; p++)
	    ;
	p++;
    }
    /* below the region: out */
    return first || so == TRRT_OUT ? TRRT_OUT : TRRT_PARTIAL;
}
