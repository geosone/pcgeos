/*
 * graphicsImage.c -- after graphicsImage.asm: GrDrawImage,
 * GrDrawHugeImage.
 */
#include "graphicsInt.h"
#include "Win/winInt.h"
#include <stdlib.h>
#include <hugearr.h>
#include "graphicsMacro.h"

#define IF_HUGE_C	0x40

static const byte imageSizes[] = { 1, 2, 4, 8, 16, 1, 1, 1 };

static void DrawImage(GrDraw *d, sword x, sword y, ImageFlags flags,
		      const Bitmap *bm, VMFileHandle file, VMBlockHandle block)
{
    GState *gs = d->GD_gs;
    CommonAttr area = gs->GS_areaAttr;
    BMFrame f;
    void *dir = NULL;

    if (GrTrivialReject(d))
	return;
    memset(&f, 0, sizeof(f));
    f.d = d;
    f.image = (ImageFlags) (flags | 0x80);	/* IF_DRAW_IMAGE */
    f.bump = imageSizes[flags & IF_BITSIZE];
    gs->GS_areaAttr.CA_mapMode |= CMM_MAP_TYPE;
    if (file) {
	HugeArrayLockDir(file, block, &dir);
	bm = (const Bitmap *) ((byte *) dir + sizeof(HugeArrayDirectory));
	f.hfile = file;
	f.hblock = block;
    }
    f.bm = bm;
    GrBitmapDraw(&f, x, y);
    GrBitmapDoneConvert(&f);
    if (f.hdata)
	HugeArrayUnlock((void *) f.hdata);
    if (dir)
	HugeArrayUnlockDir(dir);
    gs->GS_areaAttr = area;
}

void GrDrawImage(GStateHandle gstate, sword x, sword y, ImageFlags flags,
		 const Bitmap *bm)
{
    GrDraw d;

    GrEnterGraphics(gstate, &d);
    DrawImage(&d, x, y, (ImageFlags) (flags & ~IF_HUGE_C), bm, 0, 0);
    GrExitGraphics(&d);
}

void GrDrawHugeImage(GStateHandle gstate, sword x, sword y, ImageFlags flags,
		     VMFileHandle vmFile, VMBlockHandle vmBlk)
{
    GrDraw d;

    GrEnterGraphics(gstate, &d);
    DrawImage(&d, x, y, (ImageFlags) (flags | IF_HUGE_C), NULL, vmFile,
	      vmBlk);
    GrExitGraphics(&d);
}

#define IF_IGNORE_MASK	0x10

#define LINE_ATTR	((word) offsetof(GState, GS_lineAttr))

/* the color of pixel x of a scan line (after the mask) into the area
 * attributes; a key to compare runs with */
static dword ImageColor(const BMFrame *f, const byte *pix, word x,
			Boolean set)
{
    GState *gs = f->d->GD_gs;
    byte fmt = (byte) (f->args.PBA_bm.B_type & BMT_FORMAT), i;
    const byte *e;

    switch (fmt) {
    case BMF_MONO:
	i = (byte) ((pix[x >> 3] & (0x80 >> (x & 7))) ? C_BLACK : C_WHITE);
	break;
    case BMF_4BIT:
	i = (byte) ((x & 1) ? pix[x >> 1] & 0x0f : pix[x >> 1] >> 4);
	break;
    case BMF_8BIT:
	i = pix[x];
	break;
    default:
	e = pix + 3 * x;
	if (set)
	    GrSetColorIn(gs, &gs->GS_areaAttr, CF_RGB, e[0], e[1], e[2]);
	return 0x1000000UL | ((dword) e[0] << 16) | ((dword) e[1] << 8) |
	       e[2];
    }
    if (f->imgPal && fmt != BMF_MONO) {
	e = f->imgPal + 3 * (i < f->imgPalN ? i : 0);
	if (set)
	    GrSetColorIn(gs, &gs->GS_areaAttr, CF_RGB, e[0], e[1], e[2]);
    } else if (set)
	GrSetColorIn(gs, &gs->GS_areaAttr, CF_INDEX, i, 0, 0);
    return i;
}

/* DrawImageScan: one scan line at x0, y */
static void ImageScan(BMFrame *f, const byte *line, sword x0, sword y)
{
    const GrDraw *d = f->d;
    word w = f->args.PBA_bm.B_width, n = f->bump, count = 0;
    Boolean masked = (Boolean) ((f->args.PBA_bm.B_type & BMT_MASK) != 0);
    const byte *pix = line + (masked ? (w + 7) >> 3 : 0);
    Boolean border = (Boolean) ((f->image & IF_BORDER) != 0);
    sword x = x0;

    if (f->image & IF_IGNORE_MASK)
	masked = FALSE;
    while (count < w) {
	word run = 1;
	if (masked && !(line[count >> 3] & (0x80 >> (count & 7)))) {
	    while (count + run < w &&
		   !(line[(count + run) >> 3] & (0x80 >> ((count + run) & 7))))
		run++;				/* CheckMaskedPixels: skip */
	} else {
	    dword key = ImageColor(f, pix, count, TRUE);
	    while (count + run < w &&
		   (!masked || (line[(count + run) >> 3] &
				(0x80 >> ((count + run) & 7)))) &&
		   ImageColor(f, pix, (word) (count + run), FALSE) == key)
		run++;
	    GrFillRectLow(d, x, y, (sword) (x + run * n - border),
			  (sword) (y + n - border), AREA);
	}
	count = (word) (count + run);
	x = (sword) (x + run * n);
	if (x > d->GD_w->W_maskRect.R_right)
	    break;
    }
    if (border) {				/* handleBorder */
	word k;
	GrFillRectLow(d, x0, (sword) (y + n - 1), (sword) (x0 + count * n),
		      (sword) (y + n - 1), LINE_ATTR);
	for (k = 0; k < count; k++) {
	    sword bx = (sword) (x0 + n - 1 + k * n);
	    GrFillRectLow(d, bx, y, bx, (sword) (y + n - 1), LINE_ATTR);
	}
    }
}

/* ImageHugeSection: the scan lines of the slice at device line sy (the
 * bitmap's line sy - f->y) */
void ImageHugeSection(BMFrame *f, sword sy)
{
    const Window *w = f->d->GD_w;
    const byte *line = f->args.PBA_data;
    sword y = (sword) (f->y + (sy - f->y) * (sword) f->bump);
    word k;

    for (k = 0; k < f->args.PBA_bm.B_height; k++) {
	if (y > w->W_maskRect.R_bottom)
	    break;
	if (y + (sword) f->bump > w->W_maskRect.R_top)
	    ImageScan(f, line, f->x, y);
	y = (sword) (y + f->bump);
	line += f->args.PBA_size;
    }
}
