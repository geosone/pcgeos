/*
 * imMisc.c -- the routines of the input manager (after imMisc.asm:
 * ImGrabInput, ImReleaseInput, ImInfoInputProcess, ImSetDoubleClick,
 * ImInfoDoubleClick, ImGetButtonState; imPtr.asm: ImGetMousePos,
 * ImPtrJump, ImForcePtrMethod, RefreshPtr, ImBumpMouse,
 * ImConstrainMouse, EnsureConstraining, ImUnconstrainMouse; the
 * move/resize xor of imMisc.asm: ImStartMoveResize, ImDoStartScreenXor,
 * ImStopMoveResize, ImDoStopScreenXor, XorConstrain,
 * OutputUpdateScreenXor and the button end of OutputMonitor).
 *
 * The move/resize xor is a region the pointer's video driver inverts
 * and moves with the pointer (DR_VID_SET_XOR, DR_VID_CLEAR_XOR); the
 * input manager starts and stops it on its thread and keeps the pointer
 * within the minimum and maximum size of a resize.  It ends on the
 * release flags or with ImStopMoveResize.
 *
 * Positions are on the screen (DOS16 transformed them for a window
 * passed in di).  The limits of the pointer come from the root window
 * (ImSetPtrWin, imPtr.c) or, without one, from ImSetScreenLimits.
 * Not yet: the backlog
 * routines (INPUT_MESSAGE_RECEIPT is off, as in DOS16).
 */
#include "kernelC.h"
#include "imInt.h"

#include <timer.h>
#include <input.h>
#include <Internal/videoDr.h>

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

/* RefreshPtrNow on the input manager's thread: a pointer event without
 * movement through the chain, at once */
static void RefreshPtrNow(void)
{
    ImEvent ev;

    memset(&ev, 0, sizeof(ev));
    ev.IE_message = MSG_IM_PTR_CHANGE;
    ev.IE_bp = (word) (TimerGetCount() & PI_time);
    ImProcessUserInput(&ev);
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

/* --------------------------------------------------------------------
 * the move/resize xor
 */

Boolean ImStartMoveResize(const StartMoveResizeParams *params)
{
    word r = TRUE;

    if (!im.imThread)
	return TRUE;
    CObjMessageP(MF_CALL, ConstructOptr(im.imThread, 0), MSG_IM_START_XOR,
		 params, sizeof(*params), &r, sizeof(r));
    return r ? TRUE : FALSE;
}

void ImStopMoveResize(void)
{
    if (im.imThread)
	CObjMessageP(MF_CALL, ConstructOptr(im.imThread, 0),
		     MSG_IM_STOP_XOR, NULL, 0, NULL, 0);
}

/* WinTransform; the screen without a window */
static void Trans(WindowHandle win, sword *x, sword *y)
{
    XYValueAsDWord v;

    if (!win)
	return;
    v = WinTransform(win, *x, *y);
    *x = DWORD_X(v);
    *y = DWORD_Y(v);
}

/* XorConstrain */
static void XorConstrain(sword *x, sword *y)
{
    const Rectangle *c = &im.xorConstraints;

    if (c->R_left != (sword) EOREGREC && *x < c->R_left)
	*x = c->R_left;
    if (c->R_top != (sword) EOREGREC && *y < c->R_top)
	*y = c->R_top;
    if (c->R_right != (sword) EOREGREC && *x > c->R_right)
	*x = c->R_right;
    if (c->R_bottom != (sword) EOREGREC && *y > c->R_bottom)
	*y = c->R_bottom;
}

/* ImDoStopScreenXor */
static void StopXor(void)
{
    DriverRegs r;

    if (!im.inMoveResize)
	return;
    memset(&r, 0, sizeof(r));
    (void) ImCallPtrDriver(DR_VID_CLEAR_XOR, &r);
    im.inMoveResize = FALSE;
}

/* TranslateXCoord, TranslateYCoord: relative to the pointer, or in the
 * window */
static sword TransX(Boolean off, sword x, sword y, WindowHandle win)
{
    if (off)
	return (sword) (x + im.drawnX);
    Trans(win, &x, &y);
    return x;
}

static sword TransY(Boolean off, sword x, sword y, WindowHandle win)
{
    if (off)
	return (sword) (y + im.drawnY);
    Trans(win, &x, &y);
    return y;
}

/* ImDoStartScreenXor: TRUE if one is going already */
static Boolean StartXor(const StartMoveResizeParams *pp)
{
    StartMoveResizeParams p = *pp;
    VisXORParams v;
    VisXORFlags vf = 0;
    XorFlags f = p.SMRP_boxFlags;
    WindowHandle win = p.SMRP_win;
    sword l, t, r, b, ox = 0, oy = 0, x, y;
    DriverRegs dr;

    if (im.inMoveResize)
	return TRUE;
    if (p.SMRP_maxWidth <= 0)
	p.SMRP_maxWidth = 0x4000;
    if (p.SMRP_maxHeight <= 0)
	p.SMRP_maxHeight = 0x4000;
    if (p.SMRP_minWidth < 0)
	p.SMRP_minWidth = 0;
    if (p.SMRP_minHeight < 0)
	p.SMRP_minHeight = 0;
    im.xorBoxFlag = f;
    v.VXP_ax = (word) TransX((f & XF_PTR_OFF_LEFT) != 0, p.SMRP_bounds.R_left,
			     p.SMRP_bounds.R_top, win);
    v.VXP_bx = (word) TransY((f & XF_PTR_OFF_TOP) != 0, p.SMRP_bounds.R_left,
			     p.SMRP_bounds.R_top, win);
    v.VXP_cx = (word) TransX((f & XF_PTR_OFF_RIGHT) != 0,
			     p.SMRP_bounds.R_right, p.SMRP_bounds.R_bottom,
			     win);
    v.VXP_dx = (word) TransY((f & XF_PTR_OFF_BOTTOM) != 0,
			     p.SMRP_bounds.R_right, p.SMRP_bounds.R_bottom,
			     win);
    /* the limits of the pointer: none for a move */
    l = t = r = b = (sword) EOREGREC;
    if (!(f & XF_RESIZE_PENDING)) {
	if (!(f & XOR_RESIZE_ALL))
	    vf = VXF_X_POS_FOLLOWS_MOUSE | VXF_Y_POS_FOLLOWS_MOUSE;
	else {
	    if (f & XF_RESIZE_LEFT) {
		vf |= VXF_AX_PARAM_FOLLOWS_MOUSE;
		r = (sword) ((sword) v.VXP_cx - p.SMRP_minWidth);
		l = (sword) ((sword) v.VXP_cx - p.SMRP_maxWidth);
	    }
	    if (f & XF_RESIZE_TOP) {
		vf |= VXF_BX_PARAM_FOLLOWS_MOUSE;
		b = (sword) ((sword) v.VXP_dx - p.SMRP_minHeight);
		t = (sword) ((sword) v.VXP_dx - p.SMRP_maxHeight);
	    }
	    if (f & XF_RESIZE_RIGHT) {
		vf |= VXF_CX_PARAM_FOLLOWS_MOUSE;
		l = (sword) ((sword) v.VXP_ax + p.SMRP_minWidth);
		r = (sword) ((sword) v.VXP_ax + p.SMRP_maxWidth);
	    }
	    if (f & XF_RESIZE_BOTTOM) {
		vf |= VXF_DX_PARAM_FOLLOWS_MOUSE;
		t = (sword) ((sword) v.VXP_bx + p.SMRP_minHeight);
		b = (sword) ((sword) v.VXP_bx + p.SMRP_maxHeight);
	    }
	}
    }
    im.xorConstraints.R_left = l;
    im.xorConstraints.R_top = t;
    im.xorConstraints.R_right = r;
    im.xorConstraints.R_bottom = b;
    /* the parameters relative to the window's origin */
    Trans(win, &ox, &oy);
    v.VXP_ax = (word) ((sword) v.VXP_ax - ox);
    v.VXP_bx = (word) ((sword) v.VXP_bx - oy);
    v.VXP_cx = (word) ((sword) v.VXP_cx - ox);
    v.VXP_dx = (word) ((sword) v.VXP_dx - oy);
    /* the pointer into the limits now, so that the driver knows it */
    x = im.drawnX;
    y = im.drawnY;
    XorConstrain(&x, &y);
    if (x != im.drawnX || y != im.drawnY) {
	ImLockChain();
	im.pointerX = (sword) (im.pointerX + (x - im.drawnX));
	im.pointerY = (sword) (im.pointerY + (y - im.drawnY));
	ImUnlockChain();
	RefreshPtrNow();
    }
    im.xorButtonFlag = p.SMRP_releaseFlags;
    x = p.SMRP_xPos;
    y = p.SMRP_yPos;
    Trans(win, &x, &y);
    v.VXP_mousePos.P_x = x;
    v.VXP_mousePos.P_y = y;
    memset(&dr, 0, sizeof(dr));
    dr.DR_ax = (word) ox;
    dr.DR_bx = (word) oy;
    dr.DR_cx = p.SMRP_regionOffset;
    dr.DR_dx = p.SMRP_regionHandle;
    dr.DR_si = vf;
    dr.DR_bpPtr = &v;
    (void) ImCallPtrDriver(DR_VID_SET_XOR, &dr);
    im.inMoveResize = TRUE;
    return FALSE;
}

/* MSG_IM_START_XOR, MSG_IM_STOP_XOR */
void ImXorMethod(void *pself, optr oself, Message m, void *params,
		 void *reply)
{
    (void) pself; (void) oself;
    if (m == MSG_IM_START_XOR) {
	Boolean busy = StartXor(params);
	if (reply)
	    *(word *) reply = busy;
    } else
	StopXor();
}

/* OutputUpdateScreenXor: a pointer event; bp the button state */
void ImUpdateScreenXor(word bp, sword *x, sword *y)
{
    if (!(im.xorBoxFlag & (XF_NO_END_MATCH_ACTION | XF_END_MATCH_ACTION)) &&
	(bp & (BI_B3_DOWN | BI_B2_DOWN | BI_B1_DOWN | BI_B0_DOWN)) ==
	im.xorButtonFlag) {
	StopXor();
	return;
    }
    if (*x != im.drawnX || *y != im.drawnY)
	XorConstrain(x, y);
}

/* the end of the xor on a button event (OutputMonitor) */
void ImXorButton(word bp)
{
    word mask;

    if (im.xorBoxFlag & XF_NO_END_MATCH_ACTION)
	return;
    mask = (im.xorBoxFlag & XF_END_MATCH_ACTION)
	   ? (BI_PRESS | BI_DOUBLE_PRESS | BI_BUTTON)
	   : (BI_B3_DOWN | BI_B2_DOWN | BI_B1_DOWN | BI_B0_DOWN);
    if ((bp & mask) == im.xorButtonFlag)
	StopXor();
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
