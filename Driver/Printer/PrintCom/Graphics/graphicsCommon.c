/*
 * graphicsCommon.c -- the swath's huge bitmap (the C port of
 * graphicsCommon.asm): its header into the PState, its scan lines, the
 * band variables of the mode.
 *
 * Port32: the VM file is in PS_bitmapFile, the block in PS_bitmap, the
 * locked scan line's address in PS_curScanPtr (PS_curScanOffset in the
 * asm); a scan line past the end of the huge array is NULL (zeros).
 */
#include "printcom.h"
#include <hugearr.h>
#include <vm.h>

void LoadSwathHeader(PState *ps, VMFileHandle file, VMBlockHandle block)
{
    MemHandle mh;
    const byte *dir;
    const Bitmap *b;

    ps->PS_bitmapFile = file;
    ps->PS_bitmap = block;
    dir = VMLock(file, block, &mh);
    b = (const Bitmap *) (dir + sizeof(HugeArrayDirectory));
    ps->PS_swath.B_width = b->B_width;
    ps->PS_swath.B_height = b->B_height;
    ps->PS_swath.B_type = b->B_type;
    ps->PS_swath.B_compact = BMC_UNCOMPACTED;
    VMUnlock(mh);
}

/* the bytes of a scan line in the huge array: PS_bandBWidth, four
 * planes of it for CMYK */
static word ScanBytes(PState *ps)
{
    byte f = (byte) (ps->PS_swath.B_type & BMT_FORMAT);

    if (f == BMF_MONO || f == BMF_4BIT || f == BMF_8BIT || f == BMF_24BIT)
	return ps->PS_bandBWidth;
    return (word) (ps->PS_bandBWidth << 2);
}

/* the block of scan line n locked: first and last scan line in it */
static void LockScan(PState *ps, word n)
{
    void *p = NULL;
    word size;
    dword r = HugeArrayLock(ps->PS_bitmapFile, (VMBlockHandle) ps->PS_bitmap,
			    n, &p, &size);

    ps->PS_curScanNumber = n;
    ps->PS_curScanPtr = p;
    ps->PS_lastBlockScanNumber = (word) (n + HAL_COUNT(r) - 1);
    ps->PS_firstBlockScanNumber = (word) (n - (HAL_PREV(r) - 1));
}

void DerefFirstScanline(PState *ps)
{
    ps->PS_curScanNumber = 0;
    ps->PS_newScanNumber = 0;
    ps->PS_firstBlockScanNumber = 0;
    ps->PS_curScanPtr = NULL;
    ps->PS_bitmapCount = HugeArrayGetCount(ps->PS_bitmapFile,
					   (VMBlockHandle) ps->PS_bitmap);
    if (ps->PS_bitmapCount)
	LockScan(ps, 0);
}

void ReleaseScanlines(PState *ps)
{
    if (ps->PS_curScanPtr)
	HugeArrayUnlock(ps->PS_curScanPtr);
    ps->PS_curScanPtr = NULL;
}

/* DerefAScanline: scan line PS_newScanNumber, at the plane of
 * PS_curColorNumber; NULL past the end */
const byte *DerefAScanline(PState *ps)
{
    word n = ps->PS_newScanNumber;

    if (n >= ps->PS_bitmapCount)
	return NULL;
    if (!ps->PS_curScanPtr || n < ps->PS_firstBlockScanNumber ||
	n > ps->PS_lastBlockScanNumber) {
	ReleaseScanlines(ps);
	LockScan(ps, n);
	if (!ps->PS_curScanPtr)
	    return NULL;
    } else if (n != ps->PS_curScanNumber) {
	ps->PS_curScanPtr += (sdword) ((sword) (n - ps->PS_curScanNumber)) *
	    ScanBytes(ps);
	ps->PS_curScanNumber = n;
    }
    return ps->PS_curScanPtr + ps->PS_curColorNumber * ps->PS_bandBWidth;
}

/* PrLoadPstateVars: the band variables of the mode from its
 * GraphicsProperties, the band width in pixels (to bytes: 8, 2 or 1
 * pixels a byte, 3 bytes a pixel at 24 bits); the bands in the swath,
 * the scan lines left over in *left */
word PrLoadPstateVars(PState *ps, word *left)
{
    const PrinterInfo *pi = ps->PS_deviceInfo;
    const GraphicsProperties *gp = (const GraphicsProperties *)
	((const byte *) pi +
	 *(const word *) ((const byte *) &pi->PI_lowRes + ps->PS_mode));
    byte f = (byte) (ps->PS_swath.B_type & BMT_FORMAT);
    word w = ps->PS_swath.B_width;

    ps->PS_bandHeight = gp->GP_bandHeight;
    ps->PS_buffHeight = gp->GP_buffHeight;
    ps->PS_interleaveFactor = gp->GP_interleaveFactor;
    if (f == BMF_4BIT)
	w = (word) ((w + 1) & ~1);
    else if (f != BMF_8BIT && f != BMF_24BIT)
	w = (word) ((w + 7) & ~7);
    ps->PS_bandWidth = w;
    if (f == BMF_24BIT)
	w = (word) (w * 3);
    else if (f == BMF_4BIT)
	w = (word) (w >> 1);
    else if (f != BMF_8BIT)
	w = (word) (w >> 3);
    ps->PS_bandBWidth = w;
    *left = (word) (ps->PS_swath.B_height % ps->PS_bandHeight);
    return (word) (ps->PS_swath.B_height / ps->PS_bandHeight);
}
