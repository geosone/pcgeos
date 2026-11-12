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
 */
#include "vidcom.h"

static sword Transl(word v, const sword *params)
{
    if (v == EOREGREC || !((v & 0xc000) == 0x4000 || (v & 0xc000) == 0x8000))
	return (sword) v;
    return (sword) ((sword) ((v & 0x1fff) - 0x1000) +
		    (params ? params[((v >> 13) & 7) - 2] : 0));
}

Boolean VidRegionBounds(DriverRegs *r, Rectangle *b)
{
    const word *rg = r->DR_farPtr;
    const sword *params = r->DR_bpPtr;
    sword ox = (sword) (word) r->DR_ax, oy = (sword) (word) r->DR_bx;

    if (!rg)
	return FALSE;
    b->R_left = (sword) (Transl(rg[0], params) + ox);
    b->R_top = (sword) (Transl(rg[1], params) + oy);
    b->R_right = (sword) (Transl(rg[2], params) + ox);
    b->R_bottom = (sword) (Transl(rg[3], params) + oy);
    return TRUE;
}

void VidDrawRegion(DriverRegs *r)
{
    Window *w = r->DR_esDi;
    const word *p = r->DR_farPtr;
    const sword *params = r->DR_bpPtr;
    sword ox = (sword) (word) r->DR_ax, oy = (sword) (word) r->DR_bx;
    sword top, bottom;
    VidDrawState ds;
    Rectangle b;

    if (!r->DR_dsSi || !w || !VidRegionBounds(r, &b))
	return;
    VidSetupDraw(r->DR_dsSi, (const CommonAttr *) ((byte *) r->DR_dsSi +
						   (word) r->DR_si),
		 w, &ds);
    VidDevCondHidePtr(b.R_left, b.R_top, b.R_right, b.R_bottom);
    top = b.R_top;
    p += 4;
    while (*p != EOREGREC) {
	bottom = (sword) (Transl(*p++, params) + oy);
	for (; *p != EOREGREC; p += 2)
	    if (top <= bottom)
		VidFillClipped(w, (sword) (Transl(p[0], params) + ox), top,
			       (sword) (Transl(p[1], params) + ox), bottom,
			       &ds);
	p++;
	top = (sword) (bottom + 1);
    }
    VidDevCondShowPtr();
}
