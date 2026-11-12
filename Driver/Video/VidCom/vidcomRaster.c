/*
 * vidcomRaster.c -- DR_VID_GETBITS, DR_VID_PUTBITS and DR_VID_MOVBITS
 * (after vidcomRaster.asm: VidGetBits, VidPutBits; vga*Raster.asm:
 * the scan line routines; VidMovBits).
 *
 * The device is 24 bits a pixel to GEOS (BMF_24BIT: red, green, blue).
 *
 * GETBITS writes a simple Bitmap: the header, then the scan lines,
 * uncompacted, as many as fit in the block (B_height says how many).
 * The registers: ax, bx the position, cx, dx the size, the block in
 * DR_bpPtr (ds:bp) and its size in si.
 *
 * PUTBITS draws a slice of a bitmap at ax, bx: the PutBitsArgs (ss:bp)
 * in DR_bpPtr, the GState in DR_dsSi.  Each scan line is PBA_size bytes, its mask first with
 * BMT_MASK (a bit a pixel, 1 drawn).  The formats:
 *   BMF_MONO   1 black, 0 white; with PBF_FILL_MASK (GrFillBitmap) the
 *              1s in the area color with its draw mask, the 0s left
 *   BMF_4BIT   two pixels a byte, the high nibble first, an index
 *   BMF_8BIT   an index
 *   BMF_24BIT  red, green, blue
 * An index goes through the bitmap's palette (BMT_PALETTE, RGB
 * entries) or the driver's (DR_VID_SET_PALETTE).  The GState's mix mode
 * applies.
 *
 * MOVBITS moves ax, bx to cx, dx, si wide and bp high, on the screen;
 * where the destination is visible in the window's mask.  What it
 * uncovers is the caller's (WinScroll invalidates it).
 */
#include "vidcom.h"

/* GETBITS */
void VidGetBits(DriverRegs *r)
{
    sword x = (sword) (word) r->DR_ax, y = (sword) (word) r->DR_bx;
    word width = (word) r->DR_cx, height = (word) r->DR_dx;
    dword room = (word) r->DR_si, line = (dword) width * 3;
    Bitmap *bm = r->DR_bpPtr;
    byte *d;
    word n, i;
    dword p;

    if (!bm || room < sizeof(Bitmap))
	return;
    d = (byte *) (bm + 1);
    room -= sizeof(Bitmap);
    for (n = 0; n < height && room >= line; n++, room -= line)
	for (i = 0; i < width; i++) {
	    p = VidDevGetPixel((sword) (x + i), (sword) (y + n));
	    *d++ = (byte) (p >> 16);
	    *d++ = (byte) (p >> 8);
	    *d++ = (byte) p;
	}
    bm->B_width = width;
    bm->B_height = n;
    bm->B_compact = BMC_UNCOMPACTED;
    bm->B_type = BMF_24BIT;
}

/* PUTBITS */
typedef struct {
    const PutBitsArgs	*pba;
    sword		x0, y0;
    MixMode		mix;
    VidDrawState	fill;		/* PBF_FILL_MASK */
} PutCtx;

static dword PaletteColor(const PutBitsArgs *pba, byte index)
{
    const byte *e;

    if ((pba->PBA_bm.B_type & BMT_PALETTE) && pba->PBA_pal &&
	!(pba->PBA_flags & PBF_PAL_TYPE)) {
	e = pba->PBA_pal + 3 * index;
	return ((dword) e[0] << 16) | ((dword) e[1] << 8) | e[2];
    }
    return ((dword) vidPalette[index].RGB_red << 16) |
	   ((dword) vidPalette[index].RGB_green << 8) |
	   vidPalette[index].RGB_blue;
}

static void PutSpan(sword y, sword left, sword right, void *ctx)
{
    const PutCtx *c = ctx;
    const PutBitsArgs *pba = c->pba;
    word bx, by = (word) (y - c->y0), maskBytes;
    const byte *line = pba->PBA_data + (dword) by * pba->PBA_size, *data;
    byte fmt = pba->PBA_bm.B_type & BMT_FORMAT, v;
    sword x;
    dword color;

    maskBytes = (pba->PBA_bm.B_type & BMT_MASK) ?
		(word) ((pba->PBA_bm.B_width + 7) / 8) : 0;
    data = line + maskBytes;
    for (x = left; x <= right; x++) {
	bx = (word) (x - c->x0);
	if (maskBytes && !(line[bx >> 3] & (0x80 >> (bx & 7))))
	    continue;
	switch (fmt) {
	case BMF_MONO:
	    v = (byte) (data[bx >> 3] & (0x80 >> (bx & 7)));
	    if (pba->PBA_flags & PBF_FILL_MASK) {
		if (!v || (c->fill.VDS_mask &&
			   !(c->fill.VDS_mask[y & 7] & (0x80 >> (x & 7)))))
		    continue;
		VidDevPutPixel(x, y, c->fill.VDS_color, c->fill.VDS_mix);
		continue;
	    }
	    color = v ? 0 : 0xffffff;
	    break;
	case BMF_4BIT:
	    v = data[bx >> 1];
	    color = PaletteColor(pba, (byte) ((bx & 1) ? v & 0x0f : v >> 4));
	    break;
	case BMF_8BIT:
	    color = PaletteColor(pba, data[bx]);
	    break;
	case BMF_24BIT:
	    color = ((dword) data[3 * bx] << 16) |
		    ((dword) data[3 * bx + 1] << 8) | data[3 * bx + 2];
	    break;
	default:			/* CMYK: not on a screen */
	    return;
	}
	VidDevPutPixel(x, y, color, c->mix);
    }
}

void VidPutBits(DriverRegs *r)
{
    const GState *gs = r->DR_dsSi;
    Window *w = r->DR_esDi;
    PutCtx c;
    sword right, bottom;

    c.pba = r->DR_bpPtr;
    if (!c.pba || !w || !c.pba->PBA_data || !c.pba->PBA_bm.B_width ||
	!c.pba->PBA_bm.B_height)
	return;
    c.x0 = (sword) (word) r->DR_ax;
    c.y0 = (sword) (word) r->DR_bx;
    c.mix = gs ? gs->GS_mixMode : MM_COPY;
    c.fill.VDS_color = 0;
    c.fill.VDS_mix = MM_COPY;
    c.fill.VDS_mask = NULL;
    if (gs)
	VidSetupDraw(gs, &gs->GS_areaAttr, w, &c.fill);
    right = (sword) (c.x0 + c.pba->PBA_bm.B_width - 1);
    bottom = (sword) (c.y0 + c.pba->PBA_bm.B_height - 1);
    VidDevCondHidePtr(c.x0, c.y0, right, bottom);
    VidForSpans(w, c.x0, c.y0, right, bottom, FALSE, PutSpan, &c);
    VidDevCondShowPtr();
}

/* MOVBITS */
typedef struct {
    sword	ddx, ddy;		/* source = destination - (ddx, ddy) */
} MovCtx;

static void MovSpan(sword y, sword left, sword right, void *ctx)
{
    const MovCtx *c = ctx;

    VidDevCopySpan((sword) (left - c->ddx), (sword) (y - c->ddy), left, y,
		   (sword) (right - left + 1));
}

void VidMovBits(DriverRegs *r)
{
    Window *w = r->DR_esDi;
    sword sx = (sword) (word) r->DR_ax, sy = (sword) (word) r->DR_bx;
    sword dx = (sword) (word) r->DR_cx, dy = (sword) (word) r->DR_dx;
    sword width = (sword) (word) r->DR_si;
    sword height = (sword) (word) r->DR_bp;
    MovCtx c;

    if (!w || width <= 0 || height <= 0)
	return;
    c.ddx = (sword) (dx - sx);
    c.ddy = (sword) (dy - sy);
    VidDevCondHidePtr(sx < dx ? sx : dx, sy < dy ? sy : dy,
		      (sword) ((sx > dx ? sx : dx) + width - 1),
		      (sword) ((sy > dy ? sy : dy) + height - 1));
    /* down: from the bottom, so no line is overwritten before read */
    VidForSpans(w, dx, dy, (sword) (dx + width - 1),
		(sword) (dy + height - 1), c.ddy > 0, MovSpan, &c);
    VidDevCondShowPtr();
}
