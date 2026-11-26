/*
 * graphicsRasterUtils.c -- after graphicsRasterUtils.asm:
 * GrSetBitmapRes, GrGetBitmapRes, GrGetBitmapSize, GrGetHugeBitmapSize.
 */
#include "graphicsInt.h"
#include "Win/winInt.h"
#include <hugearr.h>
#include <Internal/videoDr.h>

#define DEF_BITMAP_RES	72

void GrSetBitmapRes(GStateHandle gstate, word xRes, word yRes)
{
    XYValueAsDWord old = GrGetBitmapRes(gstate);
    EditableBitmap *eb;

    if (!old)
	return;
    WinApplyScale(GrGetWinHandle(gstate), GrRatio(xRes, (word) old),
		  GrRatio(yRes, (word) (old >> 16)), WIF_DONT_INVALIDATE);
    eb = GrLockBitmap(gstate, NULL, NULL);
    if (!eb)
	return;
    eb->EB_bm.CB_xres = xRes;
    eb->EB_bm.CB_yres = yRes;
    HugeArrayDirty(eb);
    HugeArrayUnlockDir(eb);
}

/* x resolution low, y high; 0 if the GState has no bitmap */
XYValueAsDWord GrGetBitmapRes(GStateHandle gstate)
{
    EditableBitmap *eb = GrLockBitmap(gstate, NULL, NULL);
    XYValueAsDWord res;

    if (!eb)
	return 0;
    res = ((dword) eb->EB_bm.CB_yres << 16) | eb->EB_bm.CB_xres;
    HugeArrayUnlockDir(eb);
    return res;
}

/* the size in points: the pixels at 72 dpi, others scaled */
static word Points(word pixels, word res)
{
    WWFixedAsDWord v = GrMulWWFixed(GrUDivWWFixed(
				    (WWFixedAsDWord) DEF_BITMAP_RES << 16,
				    (WWFixedAsDWord) res << 16),
				    (WWFixedAsDWord) pixels << 16);

    return (word) ((v + 0x8000) >> 16);
}

XYValueAsDWord GrGetBitmapSize(const Bitmap *bm)
{
    const CBitmap *c = (const CBitmap *) bm;
    word w = bm->B_width, h = bm->B_height;

    if ((bm->B_type & BMT_COMPLEX) &&
	(c->CB_xres != DEF_BITMAP_RES || c->CB_yres != DEF_BITMAP_RES)) {
	w = Points(w, c->CB_xres);
	h = Points(h, c->CB_yres);
    }
    return ((dword) h << 16) | w;
}

XYValueAsDWord GrGetHugeBitmapSize(VMFileHandle vmFile, VMBlockHandle vmBlk)
{
    void *dir;
    XYValueAsDWord s;

    HugeArrayLockDir(vmFile, vmBlk, &dir);
    s = GrGetBitmapSize(&EB_BM(dir)->CB_simple);
    HugeArrayUnlockDir(dir);
    return s;
}
