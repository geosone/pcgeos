/*
 * vidcomPutLine.c -- DR_VID_PUTLINE (after vidcomPutLine.asm:
 * VidPutLine).
 *
 * The first scan line of a bitmap (ss:bp, PutBitsArgs) along the line
 * from ax, bx to cx, dx, as far as it reaches; clipped by the window's
 * mask pixel by pixel.  As in the asm (PutLineSetup, PutLineSegY), the
 * scan line starts at the left end of a mostly horizontal line and at
 * the top end of a mostly vertical one, whichever point that is: pixel
 * x - left, or y - top, of the scan line goes to x, y.  The kernel
 * mirrors the scan line when it wants it the other way round.
 */
#include "vidcom.h"

typedef struct {
    Window	*w;
    VidPutState	put;
    sword	left, top;		/* d_x1, putStart */
    Boolean	horiz;			/* |dx| >= |dy| */
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

    (void) k;
    p.c = c;
    for (;;) {
	p.bx = (word) (c->horiz ? x1 - c->left : y1 - c->top);
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
    c.left = x1 < x2 ? x1 : x2;
    c.top = y1 < y2 ? y1 : y2;
    c.horiz = (Boolean) ((x2 > x1 ? x2 - x1 : x1 - x2) >=
			 (y2 > y1 ? y2 - y1 : y1 - y2));
    VidDevCondHidePtr(x1 < x2 ? x1 : x2, y1 < y2 ? y1 : y2,
		      x1 < x2 ? x2 : x1, y1 < y2 ? y2 : y1);
    VidLineRuns(x1, y1, x2, y2, FALSE, PutRun, &c);
    VidDevCondShowPtr();
}
