/*
 * test_kernel_region.c -- region arithmetic of the C kernel
 * (Library/Kernel/Graphics/graphicsRegion.c).
 *
 *   - bounds, size, a point in and out, a rectangle in, out, partly in,
 *     moving a region;
 *   - AND, OR and NOT of rectangles, word for word;
 *   - random unions of rectangles: AND, OR and NOT checked point by
 *     point against the inputs, NOT NOT giving the region back;
 *   - a result too big for its room, then on chunks (GrChunkRegOp).
 */
#include "kernelC.h"
#include <Internal/grWinInt.h>
#include <lmem.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

#define E EOREGREC

static void RectReg(word *r, sword l, sword t, sword rt, sword b)
{
    r[0] = (word) (t - 1); r[1] = E;
    r[2] = (word) b; r[3] = (word) l; r[4] = (word) rt; r[5] = E;
    r[6] = E;
}

static word Words(const word *r)
{
    Rectangle b;
    return (word) (GrGetPtrRegBounds(r, &b) / 2);
}

static Boolean Same(const word *a, const word *b)
{
    word n = Words(a);
    return n == Words(b) && !memcmp(a, b, n * 2);
}

static int In(const word *r, int x, int y)
{
    return GrTestPointInReg(r, (word) x, (word) y, NULL) != 0;
}

static Boolean Op(RegionOpFlags op, const word *a, const word *b, word *out,
		  word room)
{
    word size = room;
    return (Boolean) !GrPtrRegOp(op, a, b, out, &size);
}

static dword seed = 4711;
static int Rand(int n)
{
    seed = seed * 1103515245u + 12345u;
    return (int) ((seed >> 8) % (dword) n);
}

/* a union of n random rectangles in 0..39 */
static void RandomReg(word *r, int n)
{
    static word tmp[4096], rect[7];
    int i, l, t;

    r[0] = E;
    for (i = 0; i < n; i++) {
	l = Rand(36);
	t = Rand(36);
	RectReg(rect, (sword) l, (sword) t, (sword) (l + Rand(12)),
		(sword) (t + Rand(12)));
	CHECK(Op(ROF_OR_OP, r, rect, tmp, sizeof(tmp)));
	memcpy(r, tmp, Words(tmp) * 2);
    }
}

int main(void)
{
    static word a[4096], b[4096], c[4096], d[4096];
    Rectangle bd;
    word size;
    int i, x, y, bad;
    MemHandle mh;
    ChunkHandle c1, c2, c3;

    kernelCurrentGeode = 0x2340;

    /* bounds, size, points, rectangles, moving */
    RectReg(a, 10, 20, 30, 40);
    CHECK(GrGetPtrRegBounds(a, &bd) == 14 && bd.R_left == 10 &&
	  bd.R_top == 20 && bd.R_right == 30 && bd.R_bottom == 40);
    CHECK(In(a, 10, 20) && In(a, 30, 40) && !In(a, 9, 20) &&
	  !In(a, 10, 19) && !In(a, 31, 30) && !In(a, 20, 41));
    CHECK(GrTestPointInReg(a, 15, 25, &bd) && bd.R_left == 10 &&
	  bd.R_right == 30 && bd.R_top == 20 && bd.R_bottom == 40);
    CHECK(GrTestRectInReg(a, 12, 22, 28, 38) == TRRT_IN);
    CHECK(GrTestRectInReg(a, 10, 20, 30, 40) == TRRT_IN);
    CHECK(GrTestRectInReg(a, 0, 0, 5, 5) == TRRT_OUT);
    CHECK(GrTestRectInReg(a, 0, 50, 50, 60) == TRRT_OUT);
    CHECK(GrTestRectInReg(a, 5, 25, 15, 30) == TRRT_PARTIAL);
    CHECK(GrTestRectInReg(a, 15, 35, 20, 45) == TRRT_PARTIAL);
    CHECK(GrTestRectInReg(a, 15, 10, 20, 25) == TRRT_PARTIAL);
    GrMoveReg(a, 5, -10);
    CHECK(GrGetPtrRegBounds(a, &bd) == 14 && bd.R_left == 15 &&
	  bd.R_top == 10 && bd.R_right == 35 && bd.R_bottom == 30);
    b[0] = E;
    CHECK(GrGetPtrRegBounds(b, &bd) == 2 && (word) bd.R_left == E);
    CHECK(!In(b, 0, 0) && GrTestRectInReg(b, 0, 0, 5, 5) == TRRT_OUT);

    /* AND of two rectangles: their intersection */
    RectReg(a, 0, 0, 20, 20);
    RectReg(b, 10, 10, 30, 30);
    CHECK(Op(ROF_AND_OP, a, b, c, sizeof(c)));
    RectReg(d, 10, 10, 20, 20);
    CHECK(Same(c, d));
    /* apart: nothing */
    RectReg(b, 50, 50, 60, 60);
    CHECK(Op(ROF_AND_OP, a, b, c, sizeof(c)) && c[0] == E);
    /* OR of two touching: one rectangle */
    RectReg(b, 21, 0, 40, 20);
    CHECK(Op(ROF_OR_OP, a, b, c, sizeof(c)));
    RectReg(d, 0, 0, 40, 20);
    CHECK(Same(c, d));
    RectReg(b, 0, 21, 20, 40);
    CHECK(Op(ROF_OR_OP, a, b, c, sizeof(c)));
    RectReg(d, 0, 0, 20, 40);
    CHECK(Same(c, d));
    /* OR overlapping: three bands */
    RectReg(b, 10, 10, 30, 30);
    CHECK(Op(ROF_OR_OP, a, b, c, sizeof(c)));
    {
	static const word want[] = { (word) -1, E, 9, 0, 20, E,
				     20, 0, 30, E, 30, 10, 30, E, E };
	CHECK(Words(c) == 15 && !memcmp(c, want, sizeof(want)));
    }
    /* NOT: twice is the region; of nothing, everything */
    CHECK(Op(ROF_NOT_OP, a, NULL, c, sizeof(c)));
    CHECK(!In(c, 5, 5) && In(c, 21, 5) && In(c, -100, -100) &&
	  In(c, MIN_COORD, MIN_COORD) && In(c, MAX_COORD - 1, MAX_COORD - 1) &&
	  !In(c, MAX_COORD, 0));
    CHECK(Op(ROF_NOT_OP, c, NULL, d, sizeof(d)) && Same(a, d));
    b[0] = E;
    CHECK(Op(ROF_NOT_OP, b, NULL, c, sizeof(c)) && Words(c) == 7);
    CHECK(Op(ROF_NOT_OP, c, NULL, d, sizeof(d)) && d[0] == E);

    /* random regions, checked point by point */
    for (i = 0, bad = 0; i < 200; i++) {
	RandomReg(a, 1 + Rand(6));
	RandomReg(b, 1 + Rand(6));
	CHECK(Op(ROF_AND_OP, a, b, c, sizeof(c)));
	CHECK(Op(ROF_OR_OP, a, b, d, sizeof(d)));
	for (y = -1; y < 50; y++)
	    for (x = -1; x < 50; x++)
		if (In(c, x, y) != (In(a, x, y) && In(b, x, y)) ||
		    In(d, x, y) != (In(a, x, y) || In(b, x, y)))
		    bad++;
	CHECK(Op(ROF_NOT_OP, a, NULL, c, sizeof(c)));
	for (y = -1; y < 50; y++)
	    for (x = -1; x < 50; x++)
		if (In(c, x, y) == In(a, x, y))
		    bad++;
	CHECK(Op(ROF_NOT_OP, c, NULL, d, sizeof(d)) && Same(a, d));
	/* rectangles against the points */
	x = Rand(40);
	y = Rand(40);
	{
	    int r = x + Rand(10), bt = y + Rand(10), px, py, inside = 0;
	    TestRectReturnType want;
	    for (py = y; py <= bt; py++)
		for (px = x; px <= r; px++)
		    inside += In(a, px, py);
	    want = inside == 0 ? TRRT_OUT :
		inside == (r - x + 1) * (bt - y + 1) ? TRRT_IN : TRRT_PARTIAL;
	    if (GrTestRectInReg(a, (sword) x, (sword) y, (sword) r,
				(sword) bt) != want)
		bad++;
	}
    }
    CHECK(bad == 0);

    /* too little room: says so, and how much */
    RandomReg(a, 6);
    RandomReg(b, 6);
    size = 8;
    CHECK(GrPtrRegOp(ROF_OR_OP, a, b, c, &size) && size > 8);
    CHECK(!GrPtrRegOp(ROF_OR_OP, a, b, c, &size));
    CHECK(Op(ROF_OR_OP, a, b, d, sizeof(d)) && Same(c, d));

    /* on chunks */
    mh = MemAllocLMem(LMEM_TYPE_GENERAL, 0);
    MemLock(mh);
    c1 = LMemAlloc(mh, (word) (Words(a) * 2));
    memcpy(LMemDeref(ConstructOptr(mh, c1)), a, Words(a) * 2);
    c2 = LMemAlloc(mh, (word) (Words(b) * 2));
    memcpy(LMemDeref(ConstructOptr(mh, c2)), b, Words(b) * 2);
    c3 = LMemAlloc(mh, 2);
    GrChunkRegOp(ROF_OR_OP, ConstructOptr(mh, c1), ConstructOptr(mh, c2),
		 ConstructOptr(mh, c3));
    CHECK(LMemGetChunkSize(ConstructOptr(mh, c3)) == Words(d) * 2 &&
	  Same(LMemDeref(ConstructOptr(mh, c3)), d));
    GrChunkRegOp(ROF_NOT_OP, ConstructOptr(mh, c3), 0,
		 ConstructOptr(mh, c1));
    CHECK(Op(ROF_NOT_OP, d, NULL, c, sizeof(c)) &&
	  Same(LMemDeref(ConstructOptr(mh, c1)), c));
    GrChunkRegOp(ROF_AND_OP, ConstructOptr(mh, c1), ConstructOptr(mh, c3),
		 ConstructOptr(mh, c2));
    CHECK(*(word *) LMemDeref(ConstructOptr(mh, c2)) == E);
    LMemValidate(mh);
    MemFree(mh);

    printf("%s (%d failed)\n", fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
