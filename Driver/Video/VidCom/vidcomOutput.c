/*
 * vidcomOutput.c -- DR_VID_RECT (after vidcomOutput.asm: VidDrawRect,
 * DrawComplexRect, DrawLRClippedRect, DrawSimpleRect).
 *
 * The rectangle is clipped to the bounds of the window's mask region
 * (W_maskRect).  A rectangular mask (WGF_MASK_SIMPLE) is drawn at once;
 * otherwise line by line through the clip line, the band of the mask
 * region that holds the line (WinValClipLine), a rectangle for each of
 * its on/off pairs.  The pointer is hidden while it is in the way.
 *
 * The attributes are the CommonAttr at si in the GState: the RGB value
 * (the kernel maps an index to it), the draw mask (unless AO_MASK_1),
 * and the GState's mix mode.
 */
#include "vidcom.h"

static void DrawLR(sword l, sword r, sword cl, sword cr, sword top,
		   sword bottom, const VidDrawState *ds)
{
    if (l < cl)
	l = cl;
    if (r > cr)
	r = cr;
    if (l <= r)
	VidDevFillRect(l, top, r, bottom, ds);
}

/* DrawComplexRect */
static void DrawComplexRect(Window *w, sword l, sword t, sword r,
			    sword b, const VidDrawState *ds)
{
    sword y = t, last;
    const sword *p;

    while (y <= b) {
	if (y > w->W_clipRect.R_bottom || y < w->W_clipRect.R_top)
	    WinValClipLine(w, y);
	last = w->W_clipRect.R_bottom < b ? w->W_clipRect.R_bottom : b;
	if (!(w->W_grFlags & WGF_CLIP_NULL)) {
	    if (w->W_grFlags & WGF_CLIP_SIMPLE)
		DrawLR(l, r, w->W_clipRect.R_left, w->W_clipRect.R_right,
		       y, last, ds);
	    else
		for (p = (const sword *) ((const byte *)
					  WinRegionOf(w, w->W_maskReg) +
					  w->W_clipPtr);
		     (word) p[0] != EOREGREC && p[0] <= r; p += 2)
		    if (p[1] >= l)
			DrawLR(l, r, p[0], p[1], y, last, ds);
	}
	y = (sword) (last + 1);
    }
}

void VidDrawRect(DriverRegs *r)
{
    GState *gs = r->DR_dsSi;
    const CommonAttr *ca = (const CommonAttr *) ((byte *) gs +
						 (word) r->DR_si);
    Window *w = r->DR_esDi;
    sword left = (sword) (word) r->DR_ax, top = (sword) (word) r->DR_bx;
    sword right = (sword) (word) r->DR_cx;
    sword bottom = (sword) (word) r->DR_dx;
    VidDrawState ds;

    if (!gs || !w || (w->W_grFlags & WGF_MASK_NULL))
	return;
    /* trivial reject, clip to the mask's bounds */
    if (right < w->W_maskRect.R_left || left > w->W_maskRect.R_right ||
	bottom < w->W_maskRect.R_top || top > w->W_maskRect.R_bottom)
	return;
    if (left < w->W_maskRect.R_left)
	left = w->W_maskRect.R_left;
    if (right > w->W_maskRect.R_right)
	right = w->W_maskRect.R_right;
    if (top < w->W_maskRect.R_top)
	top = w->W_maskRect.R_top;
    if (bottom > w->W_maskRect.R_bottom)
	bottom = w->W_maskRect.R_bottom;

    ds.VDS_color = ((dword) ca->CA_colorRGB.RGB_red << 16) |
		   ((dword) ca->CA_colorRGB.RGB_green << 8) |
		   ca->CA_colorRGB.RGB_blue;
    if ((ca->CA_flags & AO_MASK_1_COPY) && !(w->W_color & WCF_MASKED)) {
	ds.VDS_mix = MM_COPY;
	ds.VDS_mask = NULL;
    } else {
	ds.VDS_mix = gs->GS_mixMode;
	ds.VDS_mask = (ca->CA_flags & AO_MASK_1) ? NULL : ca->CA_mask;
    }

    VidDevCondHidePtr(left, top, right, bottom);
    if (w->W_grFlags & WGF_MASK_SIMPLE)
	VidDevFillRect(left, top, right, bottom, &ds);
    else
	DrawComplexRect(w, left, top, right, bottom, &ds);
    VidDevCondShowPtr();
}
