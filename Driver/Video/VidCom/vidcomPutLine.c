/*
 * vidcomPutLine.c -- DR_VID_PUTLINE (after vidcomPutLine.asm:
 * VidPutLine).
 *
 * The first scan line of a bitmap (ss:bp, PutBitsArgs) along the line
 * from ax, bx to cx, dx: pixel k of the line (VidLineRuns) is pixel k
 * of the scan line, as far as it reaches; clipped by the window's mask
 * pixel by pixel.  The asm reversed the scan line when x1 > x2 and drew
 * from left to right; this steps from point 1, which is the same.
 */
#include "vidcom.h"

typedef struct {
    Window	*w;
    VidPutState	put;
} PutLineCtx;

typedef struct {
    const PutLineCtx	*c;
    word		bx;
} PxCtx;

/* one pixel through the mask */
static void PxPut(sword y, sword left, sword right, void *ctx)
{
    const PxCtx *p = ctx;

    (void) right;
    VidPutBitmapPixel(&p->c->put, left, y, p->bx, 0);
}

static void PutRun(sword x1, sword y1, sword x2, sword y2, word k,
		   void *ctx)
{
    const PutLineCtx *c = ctx;
    sword sx = (sword) (x2 > x1 ? 1 : x2 < x1 ? -1 : 0);
    sword sy = (sword) (y2 > y1 ? 1 : y2 < y1 ? -1 : 0);
    PxCtx p;

    p.c = c;
    for (p.bx = k;; p.bx++) {
	if (p.bx < c->put.VPS_pba->PBA_bm.B_width)
	    VidForSpans(c->w, x1, y1, x1, y1, FALSE, PxPut, &p);
	if (x1 == x2 && y1 == y2)
	    break;
	x1 = (sword) (x1 + sx);
	y1 = (sword) (y1 + sy);
    }
}

void VidPutLine(DriverRegs *r)
{
    const PutBitsArgs *pba = r->DR_bpPtr;
    sword x1 = (sword) (word) r->DR_ax, y1 = (sword) (word) r->DR_bx;
    sword x2 = (sword) (word) r->DR_cx, y2 = (sword) (word) r->DR_dx;
    PutLineCtx c;

    c.w = r->DR_esDi;
    if (!pba || !c.w || !pba->PBA_data || !pba->PBA_bm.B_width)
	return;
    VidPutSetup(r->DR_dsSi, c.w, pba, &c.put);
    VidDevCondHidePtr(x1 < x2 ? x1 : x2, y1 < y2 ? y1 : y2,
		      x1 < x2 ? x2 : x1, y1 < y2 ? y2 : y1);
    VidLineRuns(x1, y1, x2, y2, FALSE, PutRun, &c);
    VidDevCondShowPtr();
}
