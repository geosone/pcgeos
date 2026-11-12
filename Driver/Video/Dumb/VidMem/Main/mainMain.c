/*
 * mainMain.c -- the memory video driver (after mainMain.asm; with the
 * C vidcom of Driver/Video/VidCom).
 *
 * The driver draws into bitmaps made by GrCreateBitmap: a huge array
 * whose directory block is an EditableBitmap, one scan line an element.
 * The window of such a bitmap has the array in W_bitmap; each driver
 * function finds it there (VidDevBegin), takes the directory block the
 * kernel has locked (W_bmSegment) or locks it itself, and locks the
 * scan lines as it draws (vidmemUtils.c).  DR_VID_INFO describes the
 * bitmap: its size and format, BMF_MONO while the mask is edited
 * (BM_EDIT_MASK), as VidInfo did.
 *
 * The DOS16 driver had a module per format (Mono, Clr4, Clr8, Clr24,
 * CMYK) beside the common code; here the common code is vidcom and the
 * formats are the pixel access of vidmemUtils.c: monochrome, 4, 8 and
 * 24 bits, and the mask.  Not yet: CMYK, dithering on monochrome
 * bitmaps (a color is black or white by its brightness), color
 * transfer, the palette functions (a bitmap's palette is set in its
 * header), PUTSTRING.  There is no device to set: the driver works from
 * DR_INIT on.  No pointer, no XOR region, no save under.
 */
#include "vidmem.h"

VIDEO_DRIVER_TABLE(24, 24, BMF_24BIT,
		   (DS_STANDARD << DT_DISP_SIZE_OFFSET) |
		   (DAR_NORMAL << DT_DISP_ASPECT_RATIO_OFFSET) | DC_CF_RGB);

static VideoDriverInfo info;

Boolean VidDevInit(void)
{
    MemHandle mh = GrGetPalette(0, GPT_DEFAULT);

    if (mh) {
	memcpy(vmDefaultPalette, MemLock(mh), sizeof(vmDefaultPalette));
	MemFree(mh);
    }
    DriverTable.VDI_tech = DT_MEMORY;
    DriverTable.VDI_device = 0;			/* nothing to set */
    return FALSE;
}

void VidDevExit(void)
{
    VMRelease();
}

word VidDevTest(const char *name)
{
    (void) name;
    return DP_PRESENT;
}

Boolean VidDevSet(const char *name)
{
    (void) name;
    return FALSE;
}

void VidDevScreen(Boolean on)
{
    (void) on;
}

static const byte nColors[] = { 1, 4, 8, 24 };
static const byte nBits[] = { 1, 4, 8, 8 };
static const byte nPlanes[] = { 1, 1, 1, 3 };

/* VidInfo: the bitmap of the window */
VideoDriverInfo *VidDevInfo(DriverRegs *r)
{
    const Window *w = r->DR_esDi;
    void *dir = NULL;
    const CBitmap *c;
    byte f;

    info = DriverTable;
    if (!w || !w->W_bitmap)
	return &info;
    dir = w->W_bmSegment;
    if (!dir)
	HugeArrayLockDir((VMFileHandle) OptrToHandle(w->W_bitmap),
			 (VMBlockHandle) OptrToChunk(w->W_bitmap), &dir);
    c = EB_BM(dir);
    f = (byte) (c->CB_simple.B_type & BMT_FORMAT);
    if (((EditableBitmap *) dir)->EB_flags & BM_EDIT_MASK)
	f = BMF_MONO;
    if (f > BMF_24BIT)
	f = BMF_24BIT;
    info.VDI_pageW = c->CB_simple.B_width;
    info.VDI_pageH = c->CB_simple.B_height;
    info.VDI_hRes = info.VDI_vRes = 72;
    info.VDI_bpScan = VMLineSize(c->CB_simple.B_width,
				 c->CB_simple.B_type);
    info.VDI_bmFormat = f;
    info.VDI_nColors = nColors[f];
    info.VDI_nBits = nBits[f];
    info.VDI_nPlanes = nPlanes[f];
    info.VDI_wColTab = 24;
    if (!w->W_bmSegment)
	HugeArrayUnlockDir(dir);
    return &info;
}

void VidDevBegin(DriverRegs *r)
{
    const Window *w = r->DR_esDi;

    VMRelease();
    if (w && w->W_bitmap)
	VMSelect((VMFileHandle) OptrToHandle(w->W_bitmap),
		 (VMBlockHandle) OptrToChunk(w->W_bitmap), w->W_bmSegment);
}

/* no pointer */
void VidDevSetPtr(const PointerDef *pd)
{
    (void) pd;
}

void VidDevHidePtr(void)
{
}

void VidDevShowPtr(void)
{
}

void VidDevMovePtr(sword x, sword y)
{
    (void) x;
    (void) y;
}

void VidDevCondHidePtr(sword left, sword top, sword right, sword bottom)
{
    (void) left;
    (void) top;
    (void) right;
    (void) bottom;
}

void VidDevCondShowPtr(void)
{
}
