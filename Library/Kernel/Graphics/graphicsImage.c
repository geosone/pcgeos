/*
 * graphicsImage.c -- after graphicsImage.asm: GrDrawImage,
 * GrDrawHugeImage.
 */
#include "graphicsInt.h"
#include "Win/winInt.h"
#include <stdlib.h>
#include <hugearr.h>

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
