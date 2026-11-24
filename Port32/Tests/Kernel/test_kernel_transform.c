/*
 * test_kernel_transform.c -- fixed point math and transformations of
 * the C kernel (Library/Kernel/Graphics/graphicsMath.c,
 * graphicsTransformUtils.c, graphicsTransform.c, graphicsUtils.c).
 *
 *   - products, quotients, reciprocal, square root, sine, cosine,
 *     tangent, arc sine, DWFixed products and quotients;
 *   - translation, scale, rotation, set/apply/get, null, default and
 *     saved transformations, the matrix flags;
 *   - transforming and back, as words, WWFixed, DWFixed and dwords;
 *   - the pen position;
 *   - a window's matrix and position (GrComposeMatrix).
 */
#include "kernelC.h"
#include <graphics.h>
#include <lmem.h>
#include <Internal/gstate.h>
#include <Internal/window.h>

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

#define WWF(i, f)	((WWFixedAsDWord) (((dword) (word) (i) << 16) | (word) (f)))
#define XY(x, y)	(((dword) (word) (y) << 16) | (word) (x))
#define NEAR(a, b, d)	((sdword) (a) - (sdword) (b) <= (d) && \
			 (sdword) (b) - (sdword) (a) <= (d))

static GState *Peek(GStateHandle h)
{
    GState *gs = MemLock(h);
    MemUnlock(h);
    return gs;
}

int main(void)
{
    GStateHandle g;
    GState *gs;
    TransMatrix tm;
    PointWWFixed pw;
    PointDWFixed pd;
    PointDWord pdw;
    DWFixed a, b, c;
    WWFixed wf;
    MemHandle win;
    Window *w;
    int i, bad;
    WWFixedAsDWord prev, s;

    kernelCurrentGeode = 0x2340;

    /* math */
    CHECK(GrMulWWFixed(WWF(1, 0x8000), WWF(2, 0)) == WWF(3, 0));
    CHECK(GrMulWWFixed(WWF(-2, 0x8000), WWF(0, 0x8000)) == WWF(-1, 0x4000));
    CHECK(GrMulWWFixed(1, WWF(0, 0x8000)) == 0 &&
	  GrMulWWFixed((WWFixedAsDWord) -1, WWF(0, 0x8000)) == 0);
    CHECK(GrSDivWWFixed(WWF(1, 0), WWF(3, 0)) == 0x5555);
    CHECK(GrSDivWWFixed(WWF(-1, 0), WWF(3, 0)) == (WWFixedAsDWord) -0x5555);
    CHECK(GrSDivWWFixed(WWF(0x7fff, 0), WWF(0, 0x4000)) == 0);	/* over */
    CHECK(GrUDivWWFixed(WWF(10, 0), WWF(4, 0)) == WWF(2, 0x8000));
    CHECK(GrSqrRootWWFixed(WWF(4, 0)) == WWF(2, 0));
    CHECK(NEAR(GrSqrRootWWFixed(WWF(2, 0)), 0x16a0a, 2));
    CHECK(NEAR(GrSqrRootWWFixed(WWF(10000, 0)), WWF(100, 0), 2));
    CHECK(GrSqrRootWWFixed(WWF(0, 0x8000)) == WWF(1, 0));
    CHECK(GrQuickSine(0) == 0 && GrQuickSine(WWF(90, 0)) == WWF(1, 0));
    CHECK(GrQuickSine(WWF(30, 0)) == 0x8000 &&
	  GrQuickSine(WWF(150, 0)) == 0x8000 &&
	  GrQuickSine(WWF(210, 0)) == (WWFixedAsDWord) -0x8000 &&
	  GrQuickSine(WWF(-30, 0)) == (WWFixedAsDWord) -0x8000 &&
	  GrQuickSine(WWF(390, 0)) == 0x8000);
    CHECK(GrQuickCosine(WWF(60, 0)) == 0x8000 &&
	  GrQuickCosine(WWF(180, 0)) == (WWFixedAsDWord) -0x10000L &&
	  GrQuickCosine(WWF(-60, 0)) == 0x8000);
    /* rising, but for the asm's step in the last degree (index 89) */
    for (i = 0, bad = 0, prev = 0; i < 89 * 16; i++) {
	s = GrQuickSine((WWFixedAsDWord) i << 12);
	if (s < prev)
	    bad++;
	prev = s;
    }
    CHECK(bad == 0);
    CHECK(NEAR(GrQuickTangent(WWF(45, 0)), WWF(1, 0), 2));
    CHECK(GrQuickArcSine(0x8000, 1) == WWF(30, 0));
    CHECK(GrQuickArcSine(0x8000, (word) -1) == WWF(150, 0));
    CHECK(GrQuickArcSine((WWFixedAsDWord) -0x8000, (word) -1) == WWF(210, 0));
    CHECK(GrQuickArcSine((WWFixedAsDWord) -0x8000, 1) == WWF(330, 0));
    CHECK(GrQuickArcSine(WWF(1, 0), 1) == WWF(90, 0));
    CHECK(NEAR(GrQuickArcSine(GrQuickSine(WWF(37, 0x4000)), 1),
	       WWF(37, 0x4000), 0x200));
    a.DWF_int = 100000; a.DWF_frac = 0x8000;
    b.DWF_int = -3; b.DWF_frac = 0;
    GrMulDWFixed(&a, &b, &c);
    CHECK(c.DWF_int == -300002 && c.DWF_frac == 0x8000);	/* -300001.5 */
    c.DWF_int = 300001; c.DWF_frac = 0x8000;
    wf.WWF_int = 3; wf.WWF_frac = 0;
    GrSDivDWFbyWWF(&c, &wf, &b);
    CHECK(b.DWF_int == 100000 && b.DWF_frac == 0x8000);

    /* without a window */
    g = GrCreateState(0);
    gs = Peek(g);
    CHECK(GrTransform(g, 10, 20) == XY(10, 20) &&
	  gs->GS_TMatrix.TM_flags == 0);
    GrApplyTranslation(g, WWF(5, 0), WWF(-3, 0));
    CHECK(GrTransform(g, 10, 20) == XY(15, 17) &&
	  GrUntransform(g, 15, 17) == XY(10, 20) &&
	  gs->GS_TMatrix.TM_flags == TF_TRANSLATED);
    GrApplyScale(g, WWF(2, 0), WWF(3, 0));
    CHECK(GrTransform(g, 10, 20) == XY(25, 57) &&
	  GrUntransform(g, 25, 57) == XY(10, 20) &&
	  (gs->GS_TMatrix.TM_flags & ~TF_INV_VALID) ==
	  (TF_TRANSLATED | TF_SCALED));
    GrSaveTransform(g);
    GrApplyRotation(g, WWF(90, 0));
    gs = Peek(g);			/* the chunk may have moved the block */
    CHECK(GrTransform(g, 10, 20) == XY(45, -33) &&
	  GrUntransform(g, 45, -33) == XY(10, 20) &&
	  (gs->GS_TMatrix.TM_flags & TF_ROTATED));
    for (i = 0, bad = 0; i < 200; i++) {	/* there and back */
	sword x = (sword) (i * 37 % 1000 - 500), y = (sword) (i * 91 % 800 - 400);
	XYValueAsDWord t = GrTransform(g, x, y);
	if (GrUntransform(g, DWORD_X(t), DWORD_Y(t)) != XY(x, y))
	    bad++;
    }
    CHECK(bad == 0);
    GrApplyRotation(g, WWF(30, 0));
    for (i = 0, bad = 0; i < 200; i++) {	/* rounded both ways: near */
	sword x = (sword) (i * 37 % 1000 - 500), y = (sword) (i * 91 % 800 - 400);
	XYValueAsDWord t = GrTransform(g, x, y);
	XYValueAsDWord u = GrUntransform(g, DWORD_X(t), DWORD_Y(t));
	if (!NEAR(DWORD_X(u), x, 1) || !NEAR(DWORD_Y(u), y, 1))
	    bad++;
    }
    CHECK(bad == 0);
    GrRestoreTransform(g);
    gs = Peek(g);
    CHECK(GrTransform(g, 10, 20) == XY(25, 57) && gs->GS_savedMatrix == 0);
    GrRestoreTransform(g);			/* none saved: nothing */
    GrGetTransform(g, &tm);
    CHECK(tm.TM_e11.WWF_int == 2 && tm.TM_e22.WWF_int == 3 &&
	  tm.TM_e31.DWF_int == 5 && tm.TM_e32.DWF_int == -3);
    GrSetNullTransform(g);
    CHECK(GrTransform(g, 10, 20) == XY(10, 20) &&
	  gs->GS_TMatrix.TM_flags == 0);
    GrApplyTransform(g, &tm);
    CHECK(GrTransform(g, 10, 20) == XY(25, 57));
    GrApplyTransform(g, &tm);		/* tm * tm: 2(2x+5)+5, 3(3y-3)-3 */
    CHECK(GrTransform(g, 10, 20) == XY(55, 168));
    GrSetTransform(g, &tm);
    CHECK(GrTransform(g, 10, 20) == XY(25, 57));
    GrInitDefaultTransform(g);
    GrSetNullTransform(g);
    GrSaveTransform(g);
    GrSetDefaultTransform(g);
    CHECK(GrTransform(g, 10, 20) == XY(25, 57));
    GrRestoreTransform(g);
    CHECK(GrTransform(g, 10, 20) == XY(10, 20));
    GrSetDefaultTransform(g);
    CHECK(GrTransform(g, 10, 20) == XY(25, 57));
    /* a scale of almost 1 becomes 1 (CheckForInteger) */
    GrSetNullTransform(g);
    GrApplyScale(g, 0xffff, 0x10000UL);
    gs = Peek(g);
    CHECK(gs->GS_TMatrix.TM_11.WWF_int == 1 &&
	  gs->GS_TMatrix.TM_11.WWF_frac == 0 && gs->GS_TMatrix.TM_flags == 0);

    /* WWFixed, DWFixed, dwords */
    GrApplyTranslation(g, WWF(0, 0x4000), WWF(1, 0));
    GrTransformWWFixed(g, WWF(1, 0x8000), WWF(2, 0), &pw);
    CHECK(pw.PF_x.WWF_int == 1 && pw.PF_x.WWF_frac == 0xc000 &&
	  pw.PF_y.WWF_int == 3);
    GrUntransformWWFixed(g, WWF(1, 0xc000), WWF(3, 0), &pw);
    CHECK(pw.PF_x.WWF_int == 1 && pw.PF_x.WWF_frac == 0x8000 &&
	  pw.PF_y.WWF_int == 2);
    GrSetNullTransform(g);
    GrApplyTranslationDWord(g, 100000, -100000);
    pd.PDF_x.DWF_int = 1; pd.PDF_x.DWF_frac = 0;
    pd.PDF_y.DWF_int = 2; pd.PDF_y.DWF_frac = 0x8000;
    GrTransformDWFixed(g, &pd);
    CHECK(pd.PDF_x.DWF_int == 100001 && pd.PDF_y.DWF_int == -99998 &&
	  pd.PDF_y.DWF_frac == 0x8000);
    GrUntransformDWFixed(g, &pd);
    CHECK(pd.PDF_x.DWF_int == 1 && pd.PDF_y.DWF_int == 2);
    GrTransformDWord(g, 5, 6, &pdw);
    CHECK(pdw.PD_x == 100005 && pdw.PD_y == -99994);
    GrUntransformDWord(g, 100005, -99994, &pdw);
    CHECK(pdw.PD_x == 5 && pdw.PD_y == 6);
    CHECK(GrTransform(g, 0, 0) == XY(0x8000, 0x8000));	/* overflow */

    /* the pen */
    GrSetNullTransform(g);
    GrApplyTranslation(g, WWF(7, 0), WWF(8, 0));
    GrApplyScale(g, WWF(2, 0), WWF(2, 0));
    GrMoveTo(g, 3, 4);
    gs = Peek(g);
    CHECK(gs->GS_penPos.PDF_x.DWF_int == 13 &&
	  gs->GS_penPos.PDF_y.DWF_int == 16);
    CHECK(GrGetCurPos(g) == XY(3, 4));
    GrRelMoveTo(g, WWF(1, 0), WWF(-2, 0x8000));	/* 1, -1.5 */
    CHECK(GrGetCurPos(g) == XY(4, 3));	/* 2.5 rounded */
    GrGetCurPosWWFixed(g, &pw);
    CHECK(pw.PF_x.WWF_int == 4 && pw.PF_y.WWF_int == 2 &&
	  pw.PF_y.WWF_frac == 0x8000);
    GrMoveToWWFixed(g, WWF(0, 0x8000), 0);
    GrGetCurPosWWFixed(g, &pw);
    CHECK(pw.PF_x.WWF_int == 0 && pw.PF_x.WWF_frac == 0x8000);
    GrDestroyState(g);

    /* on a window: its matrix (scale 2) and position (100, 50) */
    win = MemAllocLMem(LMEM_TYPE_WINDOW, sizeof(Window));
    w = MemLock(win);
    memset(&w->W_TMatrix, 0, sizeof(w->W_TMatrix));
    w->W_grFlags = 0;
    w->W_curState = 0;
    w->W_TMatrix.TM_11.WWF_int = 2;
    w->W_TMatrix.TM_22.WWF_int = 2;
    w->W_TMatrix.TM_flags = TF_SCALED;
    w->W_winRect.R_left = 100;
    w->W_winRect.R_top = 50;
    MemUnlock(win);
    g = GrCreateState(win);
    CHECK(GrTransform(g, 3, 4) == XY(106, 58));
    GrApplyTranslation(g, WWF(1, 0), WWF(1, 0));
    CHECK(GrTransform(g, 0, 0) == XY(102, 52) &&
	  GrUntransform(g, 102, 52) == XY(0, 0));
    w = MemLock(win);
    CHECK(w->W_curTMatrix.TM_31.DWF_int == 102 &&
	  w->W_curTMatrix.TM_22.WWF_int == 2 &&
	  !(w->W_grFlags & WGF_XFORM_VALID));
    w->W_curState = g;			/* held: a change invalidates */
    w->W_grFlags = WGF_XFORM_VALID | WGF_MASK_VALID;
    MemUnlock(win);
    GrApplyScale(g, WWF(3, 0), WWF(3, 0));
    w = MemLock(win);
    CHECK(w->W_grFlags == WGF_MASK_VALID);
    MemUnlock(win);
    CHECK(GrTransform(g, 1, 1) == XY(100 + 2 * 4, 50 + 2 * 4));
    GrDestroyState(g);
    MemFree(win);

    printf("%s (%d failed)\n", fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
