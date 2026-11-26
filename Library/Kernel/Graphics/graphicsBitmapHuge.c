/*
 * graphicsBitmapHuge.c -- after graphicsBitmapHuge.asm:
 * GrDrawHugeBitmap, GrFillHugeBitmap, GrDrawHugeBitmapAtCP,
 * GrFillHugeBitmapAtCP.
 */
#include "graphicsInt.h"
#include "Win/winInt.h"
#include <stdlib.h>
#include <hugearr.h>

/* graphicsBitmapHuge.asm: the bitmap in the huge array's directory
 * block (EditableBitmap: EB_bm after the HugeArrayDirectory), one scan
 * line per element */
static void DrawHuge(GrDraw *d, sword x, sword y, VMFileHandle file,
		     VMBlockHandle block, Boolean fill)
{
    BMFrame f;
    void *dir;

    if (GrTrivialReject(d))
	return;
    memset(&f, 0, sizeof(f));
    HugeArrayLockDir(file, block, &dir);
    f.d = d;
    f.bm = (const Bitmap *) ((byte *) dir + sizeof(HugeArrayDirectory));
    f.fill = fill;
    f.hfile = file;
    f.hblock = block;
    GrBitmapDraw(&f, x, y);
    GrBitmapDoneConvert(&f);
    if (f.hdata)
	HugeArrayUnlock((void *) f.hdata);
    HugeArrayUnlockDir(dir);
}

void GrDrawHugeBitmap(GStateHandle gstate, sword x, sword y,
		      VMFileHandle vmFile, VMBlockHandle vmBlk)
{
    GrDraw d;

    if (GrPathRect(gstate, FALSE, x, y, GrGetHugeBitmapSize(vmFile, vmBlk)))
	return;
    GrEnterGraphics(gstate, &d);
    GrSetDocPenPos(d.GD_gs, x, y);
    DrawHuge(&d, x, y, vmFile, vmBlk, FALSE);
    GrExitGraphics(&d);
}

void GrFillHugeBitmap(GStateHandle gstate, sword x, sword y,
		      VMFileHandle vmFile, VMBlockHandle vmBlk)
{
    GrDraw d;

    if (GrPathRect(gstate, FALSE, x, y, GrGetHugeBitmapSize(vmFile, vmBlk)))
	return;
    GrEnterGraphics(gstate, &d);
    GrSetDocPenPos(d.GD_gs, x, y);
    DrawHuge(&d, x, y, vmFile, vmBlk, TRUE);
    GrExitGraphics(&d);
}

static void HugeAtCP(GStateHandle gstate, VMFileHandle vmFile,
		     VMBlockHandle vmBlk, Boolean fill)
{
    XYValueAsDWord pen;
    GrDraw d;

    if (GrPathRect(gstate, TRUE, 0, 0, GrGetHugeBitmapSize(vmFile, vmBlk)))
	return;
    pen = GrGetCurPos(gstate);
    GrEnterGraphics(gstate, &d);
    DrawHuge(&d, (sword) (word) pen, (sword) (word) (pen >> 16), vmFile,
	     vmBlk, fill);
    GrExitGraphics(&d);
}

void GrDrawHugeBitmapAtCP(GStateHandle gstate, VMFileHandle vmFile,
			  VMBlockHandle vmBlk)
{
    HugeAtCP(gstate, vmFile, vmBlk, FALSE);
}

void GrFillHugeBitmapAtCP(GStateHandle gstate, VMFileHandle vmFile,
			  VMBlockHandle vmBlk)
{
    HugeAtCP(gstate, vmFile, vmBlk, TRUE);
}
