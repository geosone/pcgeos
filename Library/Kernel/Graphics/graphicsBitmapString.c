/*
 * graphicsBitmapString.c -- after graphicsBitmapString.asm: code to
 * write a bitmap out to a gstring
 */
#include "graphicsInt.h"
#include "graphicsConstant.h"
#include "graphicsMacro.h"
#include "Win/winInt.h"
#include <stdlib.h>
#include <hugearr.h>

/* SliceToString: the bytes of a bitmap (its first slice) as a graphics
 * string keeps it, the header, a complex bitmap's palette and the data
 * (packed: as far as its scan lines go); 0 if too large */
word GrBitmapStringSize(const Bitmap *bm)
{
    word lines, line = GrBitmapLineSize(bm->B_width, bm->B_type);
    const byte *data;
    dword size;

    if (bm->B_type & BMT_COMPLEX) {
	lines = GrBMComplex(bm)->CB_numScans;
	data = (const byte *) bm + GrBMComplex(bm)->CB_data;
    } else {
	lines = bm->B_height;
	data = (const byte *) (bm + 1);
    }
    if (bm->B_compact == BMC_PACKBITS || bm->B_compact == BMC_LZG) {
	byte *scratch = malloc((size_t) line * lines + 1);
	const byte *src = data;
	if (!scratch)
	    return 0;
	GrBMUnpack(bm->B_compact, &src, scratch, (dword) line * lines);
	free(scratch);
	size = (dword) (src - (const byte *) bm);
    } else
	size = (dword) (data - (const byte *) bm) + (dword) line * lines;
    return (word) (size <= 0xfff0 ? size : 0);
}

/* SliceToString of a slice after the first: GSE_BITMAP_SLICE, the size,
 * the CBitmap (CB_data right after it), the data; no palette */
static void RecordSlice(Handle gstring, const Bitmap *bm)
{
    const CBitmap *c = GrBMComplex(bm);
    word all = GrBitmapStringSize(bm), data, n;
    byte *e;
    CBitmap h;

    if (!all || all < c->CB_data)
	return;
    data = (word) (all - c->CB_data);
    n = (word) (sizeof(CBitmap) + data);
    e = malloc((size_t) n + 3);
    if (!e)
	return;
    h = *c;
    h.CB_data = sizeof(CBitmap);
    e[0] = GSE_BITMAP_SLICE;
    e[1] = (byte) n;
    e[2] = (byte) (n >> 8);
    memcpy(e + 3, &h, sizeof(h));
    memcpy(e + 3 + sizeof(h), (const byte *) bm + c->CB_data, data);
    (void) GrGSStore(gstring, e, (word) (n + 3));
    free(e);
}

/* BitmapToString: the element of a bitmap at x, y or (atCP) at the pen,
 * then the callback's slices; TRUE if the GState writes a graphics
 * string (the callback is used up then) */
Boolean BitmapToString(GStateHandle gstate, byte op, Boolean atCP,
			    sword x, sword y, const Bitmap *bm,
			    Bitmap *(*callback)(Bitmap *bm))
{
    Handle gstring = GrGStringOf(gstate);
    word n, scans;

    if (!gstring)
	return FALSE;
    if (!(n = GrBitmapStringSize(bm)))
	return TRUE;
    if (atCP)
	(void) GrGSRecord(gstate, op, "wr", n, bm, n);
    else
	(void) GrGSRecord(gstate, op, "wwwr", x, y, n, bm, n);
    if (!(bm->B_type & BMT_COMPLEX) || !callback)
	return TRUE;
    for (scans = GrBMComplex(bm)->CB_numScans; scans < bm->B_height;
	 scans = (word) (scans + GrBMComplex(bm)->CB_numScans)) {
	bm = callback((Bitmap *) bm);
	if (!bm || !GrBMComplex(bm)->CB_numScans)
	    break;
	RecordSlice(gstring, bm);
    }
    return TRUE;
}

/* BitmapToPath: while a path is defined, a bitmap is the rectangle it
 * covers (GR_DRAW_RECT; GR_DRAW_RECT_TO from the pen), of its size in
 * points (GrGetBitmapSize; the asm's ScaleBitmapSide multiplied by the
 * resolution over 72 where drawing divides); TRUE if so */
Boolean GrPathRect(GStateHandle gstate, Boolean atCP, sword x, sword y,
			XYValueAsDWord size)
{
    GState *gs = MemLock(gstate);
    Boolean path = (Boolean) (gs->GS_gstring &&
			      (gs->GS_pathFlags & PF_DEFINING_PATH));

    MemUnlock(gstate);
    if (!path)
	return FALSE;
    if (atCP) {
	XYValueAsDWord pen = GrGetCurPos(gstate);
	GrDrawRectTo(gstate, (sword) (DWORD_X(pen) + (sword) (word) size),
		     (sword) (DWORD_Y(pen) + (sword) (word) (size >> 16)));
    } else
	GrDrawRect(gstate, x, y, (sword) (x + (sword) (word) size),
		   (sword) (y + (sword) (word) (size >> 16)));
    return TRUE;
}
