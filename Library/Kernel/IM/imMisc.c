/*
 * imMisc.c -- the routines of the input manager (after imMisc.asm:
 * ImGrabInput, ImReleaseInput, ImInfoInputProcess, ImSetDoubleClick,
 * ImInfoDoubleClick, ImGetButtonState; imPtr.asm: ImGetMousePos,
 * ImPtrJump, ImForcePtrMethod, RefreshPtr, ImBumpMouse,
 * ImConstrainMouse, EnsureConstraining, ImUnconstrainMouse).
 *
 * Positions are on the screen (DOS16 transformed them for a window
 * passed in di).  The limits of the pointer come from the root window
 * (ImSetPtrWin, imPtr.c) or, without one, from ImSetScreenLimits.
 * Not yet: the move/resize xor (ImStartMoveResize), the backlog
 * routines (INPUT_MESSAGE_RECEIPT is off, as in DOS16).
 */
#include "kernelC.h"
#include "imInt.h"

#include <timer.h>

ThreadHandle ImInfoInputProcess(void)
{
    return im.imThread;
}

Boolean ImGrabInput(optr od)
{
    Boolean ok = FALSE;

    ghal_crit_enter();
    if (!im.outputOD) {
	im.outputOD = od;
	ok = TRUE;
    }
    ghal_crit_leave();
    return ok;
}

void ImReleaseInput(optr od)
{
    ghal_crit_enter();
    if (im.outputOD == od)
	im.outputOD = 0;
    ghal_crit_leave();
}

void ImSetDoubleClick(word time, word distance)
{
    im.doubleClickTime = time;
    im.doubleClickDistance = distance;
}

void ImInfoDoubleClick(word *time, word *distance)
{
    if (time)
	*time = im.doubleClickTime;
    if (distance)
	*distance = im.doubleClickDistance;
}

byte ImGetButtonState(void)
{
    return im.buttonState;
}

void ImGetMousePos(sword *x, sword *y)
{
    ImLockChain();
    if (x)
	*x = im.pointerX;
    if (y)
	*y = im.pointerY;
    ImUnlockChain();
}

/* SendToIM of a pointer change */
static void PtrChange(word x, word y, word info)
{
    IMParams p;

    if (!im.imThread)
	return;
    p.IMP_cx = x;
    p.IMP_dx = y;
    p.IMP_bp = info;
    p.IMP_si = 0;
    CObjMessageP(MF_FORCE_QUEUE, ConstructOptr(im.imThread, 0),
		 MSG_IM_PTR_CHANGE, &p, sizeof(p), NULL, 0);
}

void ImPtrJump(sword x, sword y)
{
    PtrChange((word) x, (word) y, PI_absX | PI_absY);
}

/* RefreshPtr: no movement, but through the chain (the drag timer) */
void ImForcePtrMethod(void)
{
    PtrChange(0, 0, (word) (TimerGetCount() & PI_time));
}

/* the message marks the end of the events the bump does not change; the
 * chain is held, so no event comes between */
void ImBumpMouse(Message msg, sword xBump, sword yBump)
{
    struct { word cx, dx, bp; } p;

    ImLockChain();
    if (im.outputOD) {
	p.cx = (word) xBump;
	p.dx = (word) yBump;
	p.bp = 0;
	CObjMessageP(MF_FORCE_QUEUE, im.outputOD, msg, &p, sizeof(p),
		     NULL, 0);
    }
    im.pointerX = (sword) (im.pointerX + xBump);
    im.pointerY = (sword) (im.pointerY + yBump);
    ImUnlockChain();
    ImForcePtrMethod();
}

void ImConstrainMouse(sword left, sword top, sword right, sword bottom,
		      byte buttons, optr od)
{
    static Monitor perturbMonitor;

    if (buttons && !(im.buttonState & buttons)) {
	/* the button is up already: the user let go */
	ImUnconstrainMouse();
	return;
    }
    im.constrainXMin = left;
    im.constrainYMin = top;
    im.constrainXMax = right;
    im.constrainYMax = bottom;
    im.constrainButtons = buttons;
    im.constrainOD = od;
    if (!im.perturbInstalled) {
	ImAddMonitor(&perturbMonitor, ML_PTR_PERTURB, ImPtrPerturbMonitor);
	im.perturbInstalled = TRUE;
    }
    im.constraining = TRUE;
    ImForcePtrMethod();
}

/* the monitor stays; it does nothing while not constraining */
void ImUnconstrainMouse(void)
{
    im.constraining = FALSE;
}

void ImSetScreenLimits(sword left, sword top, sword right, sword bottom)
{
    im.screenXMin = left;
    im.screenYMin = top;
    im.screenXMax = right;
    im.screenYMax = bottom;
    im.haveScreen = TRUE;
    ImForcePtrMethod();
}
