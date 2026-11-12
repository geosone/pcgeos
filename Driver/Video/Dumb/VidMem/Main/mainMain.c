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
 * transfer, PUTSTRING.
 *
 * DR_VID_SET_PALETTE and DR_VID_GET_PALETTE (VidSetPalette,
 * VidGetPalette) work on the palette in the bitmap's header; one
 * without a palette keeps none, and reading gives the default palette.
 * The asm's VidGetPalette read from the bitmap header instead of its
 * palette; the C reads the palette.  There is no device to set: the driver works from
 * DR_INIT on.  No pointer, no XOR region, no save under.
 */
#include "vidmem.h"

VIDEO_DRIVER_TABLE(24, 24, BMF_24BIT,
		   (DS_STANDARD << DT_DISP_SIZE_OFFSET) |
		   (DAR_NORMAL << DT_DISP_ASPECT_RATIO_OFFSET) | DC_CF_RGB);

static VideoDriverInfo info;

/* the window's bitmap directory: locked here if *own */
static void *BitmapDir(const Window *w, Boolean *own)
{
    void *dir;

    *own = FALSE;
    if (!w || !w->W_bitmap)
	return NULL;
    dir = w->W_bmSegment;
    if (!dir) {
	HugeArrayLockDir((VMFileHandle) OptrToHandle(w->W_bitmap),
			 (VMBlockHandle) OptrToChunk(w->W_bitmap), &dir);
	*own = TRUE;
    }
    return dir;
}

/* VidSetPalette, VidGetPalette: al the first entry, cx the count, dx:si
 * the RGB values */
static Boolean BitmapPalette(DriverRegs *r, Boolean set)
{
    const Window *w = r->DR_esDi;
    byte *rgb = r->DR_dsSi;
    word first = (byte) r->DR_ax, n = (word) r->DR_cx, have = 256;
    Boolean own;
    void *dir = BitmapDir(w, &own);
    CBitmap *c;
    byte *p = vmDefaultPalette;

    if (!dir || !rgb)
	return TRUE;
    c = (CBitmap *) EB_BM(dir);
    if (c->CB_palette) {
	byte *pal = (byte *) c + c->CB_palette;
	have = (word) (pal[0] | (pal[1] << 8));
	p = pal + 2;
    }
    if (first >= have)
	n = 0;
    else if (first + n > have)
	n = (word) (have - first);
    if (set) {
	if (c->CB_palette && n) {
	    memcpy(p + 3 * first, rgb, (size_t) n * 3);
	    HugeArrayDirty(dir);
	}
    } else {
	memcpy(rgb, p + 3 * first, (size_t) n * 3);
	r->DR_cx = c->CB_palette ? n : 0;
    }
    if (own)
	HugeArrayUnlockDir(dir);
    /* the bitmap selected for drawing reads its palette again */
    VMRelease();
    return TRUE;
}

Boolean VidDevInit(void)
{
    MemHandle mh = GrGetPalette(0, GPT_DEFAULT);

    if (mh) {
	/* the number of entries, then the entries */
	memcpy(vmDefaultPalette, (byte *) MemLock(mh) + 2,
	       sizeof(vmDefaultPalette));
	MemFree(mh);
    }
    vidDevPalette = BitmapPalette;
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
    Boolean own;

    info = DriverTable;
    if (!(dir = BitmapDir(w, &own)))
	return &info;
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
    if (own)
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
