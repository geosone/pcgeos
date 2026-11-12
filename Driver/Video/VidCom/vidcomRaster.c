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

int VidBitmapPixel(const PutBitsArgs *pba, word bx, word by,
		   dword *color)
{
    const byte *line, *data;
    word maskBytes;
    byte v;

    if (bx >= pba->PBA_bm.B_width || by >= pba->PBA_bm.B_height)
	return VBP_SKIP;
    line = pba->PBA_data + (dword) by * pba->PBA_size;
    maskBytes = (pba->PBA_bm.B_type & BMT_MASK) ?
		(word) ((pba->PBA_bm.B_width + 7) / 8) : 0;
    data = line + maskBytes;
    if (maskBytes && !(line[bx >> 3] & (0x80 >> (bx & 7))))
	return VBP_SKIP;
    switch (pba->PBA_bm.B_type & BMT_FORMAT) {
    case BMF_MONO:
	v = (byte) (data[bx >> 3] & (0x80 >> (bx & 7)));
	if (pba->PBA_flags & PBF_FILL_MASK)
	    return v ? VBP_FILL : VBP_SKIP;
	*color = v ? 0 : 0xffffff;
	return VBP_COLOR;
    case BMF_4BIT:
	v = data[bx >> 1];
	*color = PaletteColor(pba, (byte) ((bx & 1) ? v & 0x0f : v >> 4));
	return VBP_COLOR;
    case BMF_8BIT:
	*color = PaletteColor(pba, data[bx]);
	return VBP_COLOR;
    case BMF_24BIT:
	*color = ((dword) data[3 * bx] << 16) |
		 ((dword) data[3 * bx + 1] << 8) | data[3 * bx + 2];
	return VBP_COLOR;
    default:				/* CMYK: not on a screen */
	return VBP_SKIP;
    }
}

void VidPutBitmapPixel(const VidPutState *c, sword x, sword y, word bx,
		       word by)
{
    dword color;

    switch (VidBitmapPixel(c->VPS_pba, bx, by, &color)) {
    case VBP_COLOR:
	VidDevPutPixel(x, y, color, c->VPS_mix);
	break;
    case VBP_FILL:
	if (!c->VPS_fill.VDS_mask ||
	    (c->VPS_fill.VDS_mask[y & 7] & (0x80 >> (x & 7))))
	    VidDevPutPixel(x, y, c->VPS_fill.VDS_color, c->VPS_fill.VDS_mix);
	break;
    }
}

void VidPutSetup(const GState *gs, const Window *w, const PutBitsArgs *pba,
		 VidPutState *c)
{
    c->VPS_pba = pba;
    c->VPS_mix = gs ? gs->GS_mixMode : MM_COPY;
    c->VPS_fill.VDS_color = 0;
    c->VPS_fill.VDS_mix = MM_COPY;
    c->VPS_fill.VDS_mask = NULL;
    if (gs)
	VidSetupDraw(gs, &gs->GS_areaAttr, w, &c->VPS_fill);
}

typedef struct {
    VidPutState	put;
    sword	x0, y0;
} PutCtx;

static void PutSpan(sword y, sword left, sword right, void *ctx)
{
    const PutCtx *c = ctx;
    sword x;

    for (x = left; x <= right; x++)
	VidPutBitmapPixel(&c->put, x, y, (word) (x - c->x0),
			  (word) (y - c->y0));
}

void VidPutBits(DriverRegs *r)
{
    const PutBitsArgs *pba = r->DR_bpPtr;
    Window *w = r->DR_esDi;
    PutCtx c;
    sword right, bottom;

    if (!pba || !w || !pba->PBA_data || !pba->PBA_bm.B_width ||
	!pba->PBA_bm.B_height)
	return;
    VidPutSetup(r->DR_dsSi, w, pba, &c.put);
    c.x0 = (sword) (word) r->DR_ax;
    c.y0 = (sword) (word) r->DR_bx;
    right = (sword) (c.x0 + pba->PBA_bm.B_width - 1);
    bottom = (sword) (c.y0 + pba->PBA_bm.B_height - 1);
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
