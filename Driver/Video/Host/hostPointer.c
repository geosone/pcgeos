/*
 * hostPointer.c -- the pointer of the host video driver (after
 * vga24Pointer.asm: VidHidePtr, VidShowPtr, VidMovePtr, VidSetPtr,
 * CondHidePtr, CondShowPtr, DrawCursor, EraseCursor; the default
 * pointer pBasic of vidcomTables.asm).
 *
 * The pointer is drawn into the surface; what was under it is kept and
 * put back.  It starts hidden (a count of 1): DR_VID_SHOWPTR shows it.
 * The picture is a PointerDef with its mask and image, up to 32x32
 * (DOS16 took 16x16 only): mask 0 image 0 leaves the screen, 0 1
 * inverts it, 1 0 is black, 1 1 white.  The position is the hot spot.
 */
#include "host.h"

#define PTR_MAX	32

static byte cursorCount = 1;		/* 0: shown */
static Boolean hiddenFlag;		/* CondHidePtr hid it */
static sword cursorX, cursorY;
static sbyte hotX, hotY;
static byte ptrW = 16, ptrH = 16;
static dword ptrMask[PTR_MAX], ptrImage[PTR_MAX];	/* bit 31 left */
/* where it is drawn and what was there */
static Boolean drawn;
static sword drawnL, drawnT;
static uint32_t under[PTR_MAX * PTR_MAX];

static const byte pBasic[sizeof(PointerDef) + 64] = {
    16, 16, 0, 0,
    /* mask */
    0xe0, 0x00, 0xf8, 0x00, 0xfe, 0x00, 0x7f, 0x80,
    0x7f, 0xe0, 0x3f, 0xf8, 0x3f, 0xfc, 0x1f, 0xfc,
    0x1f, 0xf8, 0x0f, 0xf0, 0x0f, 0xf8, 0x07, 0xfc,
    0x07, 0xbe, 0x03, 0x1f, 0x00, 0x0f, 0x00, 0x07,
    /* image */
    0xe0, 0x00, 0x98, 0x00, 0x86, 0x00, 0x41, 0x80,
    0x40, 0x60, 0x20, 0x18, 0x20, 0x04, 0x10, 0x04,
    0x10, 0x38, 0x08, 0x10, 0x08, 0x88, 0x04, 0xc4,
    0x04, 0xa2, 0x03, 0x11, 0x00, 0x09, 0x00, 0x07
};

static void EraseCursor(void)
{
    sword x, y, l = drawnL, t = drawnT;
    const uint32_t *u = under;

    if (!drawn)
	return;
    drawn = FALSE;
    for (y = 0; y < ptrH; y++)
	for (x = 0; x < ptrW; x++, u++)
	    if (l + x >= 0 && t + y >= 0 && l + x < (sword) hostSurf.width &&
		t + y < (sword) hostSurf.height)
		*HostPixel(l + x, t + y) = *u;
    HostDirty(l < 0 ? 0 : l, t < 0 ? 0 : t,
	      (sword) (l + ptrW - 1), (sword) (t + ptrH - 1));
}

static void DrawCursor(void)
{
    sword x, y, l = (sword) (cursorX - hotX), t = (sword) (cursorY - hotY);
    uint32_t *u = under, *p;
    dword bit;

    if (!hostSurf.pixels || drawn)
	return;
    drawn = TRUE;
    drawnL = l;
    drawnT = t;
    for (y = 0; y < ptrH; y++)
	for (x = 0, bit = 0x80000000UL; x < ptrW; x++, u++, bit >>= 1) {
	    if (l + x < 0 || t + y < 0 || l + x >= (sword) hostSurf.width ||
		t + y >= (sword) hostSurf.height)
		continue;
	    p = HostPixel(l + x, t + y);
	    *u = *p;
	    if (ptrMask[y] & bit)
		*p = (ptrImage[y] & bit) ? 0xffffff : 0;
	    else if (ptrImage[y] & bit)
		*p ^= 0xffffff;
	}
    if (l + ptrW > 0 && t + ptrH > 0)
	HostDirty(l < 0 ? 0 : l, t < 0 ? 0 : t,
		  (sword) (l + ptrW - 1 < hostSurf.width - 1 ?
			   l + ptrW - 1 : hostSurf.width - 1),
		  (sword) (t + ptrH - 1 < hostSurf.height - 1 ?
			   t + ptrH - 1 : hostSurf.height - 1));
}

void HostPtrReset(void)
{
    drawn = FALSE;
    hiddenFlag = FALSE;
    cursorCount = 1;
}

void VidDevHidePtr(void)
{
    if (++cursorCount == 1)
	EraseCursor();
}

void VidDevShowPtr(void)
{
    if (!cursorCount)
	return;				/* EC: underflow */
    if (!--cursorCount)
	DrawCursor();
}

void VidDevMovePtr(sword x, sword y)
{
    if (cursorCount) {
	cursorX = x;
	cursorY = y;
	return;
    }
    EraseCursor();
    cursorX = x;
    cursorY = y;
    DrawCursor();
}

void VidDevSetPtr(const PointerDef *pd)
{
    const byte *d;
    word bpl, y, b;

    if (!cursorCount)
	EraseCursor();
    if (!pd)
	pd = (const PointerDef *) pBasic;
    ptrW = (byte) (pd->PD_width & PDW_WIDTH);
    ptrH = pd->PD_height;
    if (ptrW > PTR_MAX)
	ptrW = PTR_MAX;
    if (ptrH > PTR_MAX)
	ptrH = PTR_MAX;
    hotX = pd->PD_hotX;
    hotY = pd->PD_hotY;
    bpl = (word) (((pd->PD_width & PDW_WIDTH) + 7) / 8);
    d = (const byte *) (pd + 1);
    memset(ptrMask, 0, sizeof(ptrMask));
    memset(ptrImage, 0, sizeof(ptrImage));
    for (y = 0; y < ptrH; y++)
	for (b = 0; b < bpl && b < 4; b++) {
	    ptrMask[y] |= (dword) d[y * bpl + b] << (24 - 8 * b);
	    ptrImage[y] |= (dword) d[(pd->PD_height + y) * bpl + b] <<
			   (24 - 8 * b);
	}
    if (!cursorCount)
	DrawCursor();
}

void VidDevCondHidePtr(sword left, sword top, sword right, sword bottom)
{
    if (cursorCount || hiddenFlag || !drawn)
	return;
    if (right < drawnL || left > drawnL + ptrW - 1 ||
	bottom < drawnT || top > drawnT + ptrH - 1)
	return;
    hiddenFlag = TRUE;
    EraseCursor();
}

void VidDevCondShowPtr(void)
{
    if (!hiddenFlag)
	return;
    hiddenFlag = FALSE;
    DrawCursor();
}
