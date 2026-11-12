/*
 * imPtr.c -- the pointer on the screen (after imPtr.asm: ImSetPtrWin,
 * ImSetPtrImage, LockPtrImageCommon, SetPtrImageLow, CallMovePtr).
 *
 * The pointer is on a root window (ImSetPtrWin): its bounds limit the
 * pointer, and its video driver draws it (DR_VID_MOVEPTR, which also
 * names the save under areas under it for WinMovePtr; DR_VID_SETPTR).
 *
 * The image comes from levels (PtrImageLevel, win.h; PIL_SYSTEM first):
 * the window system sets PIL_GEODE, PIL_GADGET and PIL_WINDOW for the
 * window under the pointer (winNotification.c), the UI the others.  The
 * first level with an image is shown, the driver's default if none.
 * An image is an optr to a PointerDef chunk, locked while the driver
 * takes it.
 *
 * ImSetPtrFlags: with PF_HIDE_PTR_IF_NOT_OF_ALWAYS_SHOW_TYPE the pointer
 * is hidden (DR_VID_HIDEPTR) unless its image has PDW_ALWAYS_SHOW_PTR
 * (the busy pointer); a disembodied pointer (PF_DISEMBODIED_PTR) does
 * not follow the mouse, only the save under areas under it are checked
 * (DR_VID_CHECK_UNDER).
 *
 * Not yet: re-centering a pen's pointer.
 */
#include "kernelC.h"
#include "imInt.h"

#include <win.h>
#include <lmem.h>
#include <Internal/videoDr.h>

static KSem semPtrImage = { 1, 0 };

#define CallPtrDriver ImCallPtrDriver

Boolean ImCallPtrDriver(word function, DriverRegs *r)
{
    DriverStrategy *s = (DriverStrategy *) im.ptrStrategy;

    if (!s)
	return TRUE;
    r->DR_esDi = NULL;
    return s(function, r);
}

/* CallMovePtr: the save under areas under the pointer; a disembodied
 * one stays, the areas at x, y are checked */
word ImCallMovePtr(sword x, sword y)
{
    DriverRegs r;
    word function = DR_VID_MOVEPTR;

    memset(&r, 0, sizeof(r));
    r.DR_ax = (word) x;
    r.DR_bx = (word) y;
    if (im.ptrFlags & PF_DISEMBODIED_PTR) {
	function = DR_VID_CHECK_UNDER;
	r.DR_cx = (word) x;
	r.DR_dx = (word) y;
    }
    if (CallPtrDriver(function, &r))
	return 0;
    return (word) r.DR_ax;
}

/* ShowPtrCommon, HidePtrIfWarrantedCommon (def: the image, NULL for the
 * default) */
static void ShowPtr(void)
{
    DriverRegs r;

    if (!im.hidingPtr)
	return;
    memset(&r, 0, sizeof(r));
    (void) CallPtrDriver(DR_VID_SHOWPTR, &r);
    im.hidingPtr = FALSE;
}

static void HidePtrIfWarranted(const PointerDef *def)
{
    DriverRegs r;

    if ((def && (def->PD_width & PDW_ALWAYS_SHOW_PTR)) || im.hidingPtr)
	return;
    memset(&r, 0, sizeof(r));
    (void) CallPtrDriver(DR_VID_HIDEPTR, &r);
    im.hidingPtr = TRUE;
}

void ImSetPtrWin(WindowHandle root, sword x, sword y)
{
    DriverRegs r;
    Rectangle b;
    void *strategy = NULL;

    memset(&r, 0, sizeof(r));
    (void) CallPtrDriver(DR_VID_HIDEPTR, &r);
    WinGetWinScreenBounds(root, &b);
    WinGetInfo(root, WIT_STRATEGY, &strategy);
    ghal_crit_enter();
    im.pointerWin = root;
    im.ptrStrategy = strategy;
    im.screenXMin = b.R_left;
    im.screenYMin = b.R_top;
    im.screenXMax = b.R_right;
    im.screenYMax = b.R_bottom;
    im.haveScreen = TRUE;
    im.pointerX = im.displayX = x;
    im.pointerY = im.displayY = y;
    im.drawnValid = FALSE;		/* WinMovePtr for the new tree */
    ghal_crit_leave();
    /* not DR_VID_SHOWPTR: the image may not be set for this driver
     * yet; the caller shows it */
    (void) ImCallMovePtr(x, y);
    ImForcePtrMethod();
}

/* SetPtrImageLow: the image to the driver (NULL: its default) */
static void SetPtrImageLow(optr image)
{
    DriverRegs r;
    MemHandle h = OptrToHandle(image);

    memset(&r, 0, sizeof(r));
    if (h) {
	MemLock(h);
	r.DR_dsSi = LMemDerefHandles(h, OptrToChunk(image));
    }
    if (im.ptrFlags & PF_HIDE_PTR_IF_NOT_OF_ALWAYS_SHOW_TYPE)
	HidePtrIfWarranted(r.DR_dsSi);
    (void) CallPtrDriver(DR_VID_SETPTR, &r);
    if ((im.ptrFlags & PF_HIDE_PTR_IF_NOT_OF_ALWAYS_SHOW_TYPE) && h &&
	(((const PointerDef *) LMemDerefHandles(h, OptrToChunk(image)))->
	 PD_width & PDW_ALWAYS_SHOW_PTR))
	ShowPtr();
    if (h)
	MemUnlock(h);
}

PtrFlags ImSetPtrFlags(PtrFlags set, PtrFlags clear)
{
    PtrFlags old;
    byte now;

    KSemP(&semPtrImage, KSEM_FOREVER);
    old = im.ptrFlags;
    now = (byte) ((old | set) & ~clear);
    im.ptrFlags = now;
    if ((old ^ now) & PF_HIDE_PTR_IF_NOT_OF_ALWAYS_SHOW_TYPE) {
	if (now & PF_HIDE_PTR_IF_NOT_OF_ALWAYS_SHOW_TYPE) {
	    MemHandle h = OptrToHandle(im.curPtrImage);
	    const PointerDef *def = NULL;
	    if (h) {
		MemLock(h);
		def = LMemDerefHandles(h, OptrToChunk(im.curPtrImage));
	    }
	    HidePtrIfWarranted(def);
	    if (h)
		MemUnlock(h);
	} else
	    ShowPtr();
    }
    KSemV(&semPtrImage);
    return old;
}

PtrFlags ImGetPtrFlags(void)
{
    return im.ptrFlags;
}

void ImSetPtrImage(word level, optr ptrDef)
{
    word i;
    optr use = 0;

    if (level >= IM_NUM_PTR_IMAGE_LEVELS)
	return;
    KSemP(&semPtrImage, KSEM_FOREVER);
    if (!OptrToHandle(ptrDef) && OptrToChunk(ptrDef) == PIV_UPDATE)
	im.curPtrValid = FALSE;		/* load it again */
    else if (im.ptrImages[level] == ptrDef) {
	KSemV(&semPtrImage);
	return;
    } else
	im.ptrImages[level] = ptrDef;
    /* PIV_VIDEO_DRIVER_DEFAULT is stored as it is, without a handle:
     * like none */
    for (i = 0; i < IM_NUM_PTR_IMAGE_LEVELS; i++)
	if (OptrToHandle(im.ptrImages[i])) {
	    use = im.ptrImages[i];
	    break;
	}
    if (!im.curPtrValid || im.curPtrImage != use) {
	im.curPtrImage = use;
	im.curPtrValid = TRUE;
	SetPtrImageLow(use);
    }
    KSemV(&semPtrImage);
}
