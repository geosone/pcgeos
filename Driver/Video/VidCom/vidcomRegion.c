/*
 * vidcomRegion.c -- DR_VID_REGION (after vidcomRegion.asm:
 * VidDrawRegion, TransRegCoord).
 *
 * A region (dx:cx) is its bounds, then lines of words: the bottom y of
 * a band, its on/off x pairs, EOREGREC; one more EOREGREC ends it.  The
 * first band starts at the bounds' top.  It is drawn moved by ax, bx,
 * each pair of a band a rectangle through the window's mask.
 *
 * A coordinate from 0x4000 to 0xbfff takes a parameter (graphics.def):
 * the top three bits pick PARAM_0 ... PARAM_3 of the four words at
 * ss:bp (DR_bpPtr), the rest is an offset from 0x1000.
 *
 * VidRegionWalk gives the rectangles of a region; the XOR region
 * (vidcomXOR.c) is drawn with it too.
 */
#include "vidcom.h"

static sword Transl(word v, const sword *params)
{
    if (v == EOREGREC || !((v & 0xc000) == 0x4000 || (v & 0xc000) == 0x8000))
	return (sword) v;
    return (sword) ((sword) ((v & 0x1fff) - 0x1000) +
		    (params ? params[((v >> 13) & 7) - 2] : 0));
}

void VidRegionTransBounds(const word *rg, const sword *params, sword ox,
			  sword oy, Rectangle *b)
{
    b->R_left = (sword) (Transl(rg[0], params) + ox);
    b->R_top = (sword) (Transl(rg[1], params) + oy);
    b->R_right = (sword) (Transl(rg[2], params) + ox);
    b->R_bottom = (sword) (Transl(rg[3], params) + oy);
}

void VidRegionWalk(const word *p, const sword *params, sword ox, sword oy,
		   VidRectFn *fn, void *ctx)
{
    sword top, bottom;

    top = (sword) (Transl(p[1], params) + oy);
    p += 4;
    while (*p != EOREGREC) {
	bottom = (sword) (Transl(*p++, params) + oy);
	for (; *p != EOREGREC; p += 2)
	    if (top <= bottom)
		fn((sword) (Transl(p[0], params) + ox), top,
		   (sword) (Transl(p[1], params) + ox), bottom, ctx);
	p++;
	top = (sword) (bottom + 1);
    }
}

Boolean VidRegionBounds(DriverRegs *r, Rectangle *b)
{
    if (!r->DR_farPtr)
	return FALSE;
    VidRegionTransBounds(r->DR_farPtr, r->DR_bpPtr,
			 (sword) (word) r->DR_ax, (sword) (word) r->DR_bx, b);
    return TRUE;
}

typedef struct {
    Window		*w;
    VidDrawState	ds;
} RegionCtx;

static void Fill(sword l, sword t, sword r, sword b, void *ctx)
{
    RegionCtx *c = ctx;

    VidFillClipped(c->w, l, t, r, b, &c->ds);
}

void VidDrawRegion(DriverRegs *r)
{
    RegionCtx c;
    Rectangle b;

    c.w = r->DR_esDi;
    if (!r->DR_dsSi || !c.w || !VidRegionBounds(r, &b))
	return;
    VidSetupDraw(r->DR_dsSi, (const CommonAttr *) ((byte *) r->DR_dsSi +
						   (word) r->DR_si),
		 c.w, &c.ds);
    VidDevCondHidePtr(b.R_left, b.R_top, b.R_right, b.R_bottom);
    VidRegionWalk(r->DR_farPtr, r->DR_bpPtr, (sword) (word) r->DR_ax,
		  (sword) (word) r->DR_bx, Fill, &c);
    VidDevCondShowPtr();
}
