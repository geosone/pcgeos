/*
 * vidcomXOR.c -- the XOR region (after vidcomXOR.asm: VidSetXOR,
 * VidClearXOR, UpdateXORForPtr, UpdateXORParams, CheckXORCollision,
 * HideXOR, ShowXOR, DrawXOR).
 *
 * A region inverted on the screen that can follow the pointer: the
 * frame while a window is moved or resized.  DR_VID_SET_XOR takes its
 * position (ax, bx), the region (dx:cx, a handle and the offset in its
 * block, locked while the region is set), VisXORFlags (si) and
 * VisXORParams (ss:bp, DR_bpPtr): the region's parameters and the
 * pointer position they are for.  On DR_VID_MOVEPTR the position and
 * parameters the flags name move with the pointer.  DR_VID_CLEAR_XOR
 * removes it and returns the parameters (ax..dx) and the position (si,
 * di) as they are then.
 *
 * It is drawn over everything, clipped to the screen only, and taken
 * away around drawing that touches it (the strategy routine).  The asm
 * moved it only while the pointer was shown; here it always follows.
 */
#include "vidcom.h"

#include <heap.h>

static Handle xorRegionHandle;
static const word *xorRegion;
static sword xorX, xorY;
static sword xorParams[4];
static VisXORFlags xorFlags;
static Boolean xorHidden;
static Rectangle xorBounds;

static void Invert(sword l, sword t, sword r, sword b, void *ctx)
{
    (void) ctx;
    VidDevFillRect(l, t, r, b, &vidInvertState);
}

/* DrawXOR */
static void DrawXOR(void)
{
    VidRegionTransBounds(xorRegion, xorParams, xorX, xorY, &xorBounds);
    VidDevCondHidePtr(xorBounds.R_left, xorBounds.R_top,
		      xorBounds.R_right, xorBounds.R_bottom);
    VidRegionWalk(xorRegion, xorParams, xorX, xorY, Invert, NULL);
    VidDevCondShowPtr();
}

void VidHideXOR(void)
{
    if (!xorRegion || xorHidden)
	return;
    DrawXOR();
    xorHidden = TRUE;
}

void VidShowXOR(void)
{
    if (!xorRegion || !xorHidden)
	return;
    DrawXOR();
    xorHidden = FALSE;
}

/* CheckXORCollision: a drawing operation in these bounds */
void VidCheckXORCollision(sword l, sword t, sword r, sword b)
{
    if (!xorRegion || xorHidden)
	return;
    if (l > xorBounds.R_right || r < xorBounds.R_left ||
	t > xorBounds.R_bottom || b < xorBounds.R_top)
	return;
    VidHideXOR();
}

static void UpdateXORParams(sword dx, sword dy)
{
    if (xorFlags & VXF_X_POS_FOLLOWS_MOUSE)
	xorX = (sword) (xorX + dx);
    if (xorFlags & VXF_Y_POS_FOLLOWS_MOUSE)
	xorY = (sword) (xorY + dy);
    if (xorFlags & VXF_AX_PARAM_FOLLOWS_MOUSE)
	xorParams[0] = (sword) (xorParams[0] + dx);
    if (xorFlags & VXF_BX_PARAM_FOLLOWS_MOUSE)
	xorParams[1] = (sword) (xorParams[1] + dy);
    if (xorFlags & VXF_CX_PARAM_FOLLOWS_MOUSE)
	xorParams[2] = (sword) (xorParams[2] + dx);
    if (xorFlags & VXF_DX_PARAM_FOLLOWS_MOUSE)
	xorParams[3] = (sword) (xorParams[3] + dy);
}

/* UpdateXORForPtr */
void VidXORPtrMoved(sword dx, sword dy)
{
    Boolean shown;

    if (!xorRegion || !(dx || dy) || !xorFlags)
	return;
    shown = !xorHidden;
    VidHideXOR();
    UpdateXORParams(dx, dy);
    if (shown)
	VidShowXOR();
}

static void Unlock(void)
{
    if (xorRegionHandle)
	MemUnlock(xorRegionHandle);
    xorRegionHandle = 0;
    xorRegion = NULL;
}

void VidSetXOR(DriverRegs *r)
{
    const VisXORParams *p = r->DR_bpPtr;
    byte *block;

    VidHideXOR();
    Unlock();
    xorX = (sword) (word) r->DR_ax;
    xorY = (sword) (word) r->DR_bx;
    xorFlags = (VisXORFlags) r->DR_si;
    if (p) {
	xorParams[0] = (sword) p->VXP_ax;
	xorParams[1] = (sword) p->VXP_bx;
	xorParams[2] = (sword) p->VXP_cx;
	xorParams[3] = (sword) p->VXP_dx;
    }
    xorRegionHandle = (Handle) r->DR_dx;
    block = xorRegionHandle ? MemLock(xorRegionHandle) : NULL;
    if (!block) {
	xorRegionHandle = 0;
	return;
    }
    xorRegion = (const word *) (block + (word) r->DR_cx);
    if (p)
	UpdateXORParams((sword) (vidPtrX - p->VXP_mousePos.P_x),
			(sword) (vidPtrY - p->VXP_mousePos.P_y));
    xorHidden = TRUE;
    VidShowXOR();
}

void VidClearXOR(DriverRegs *r)
{
    VidHideXOR();
    xorFlags = 0;
    Unlock();
    xorHidden = FALSE;
    r->DR_ax = (word) xorParams[0];
    r->DR_bx = (word) xorParams[1];
    r->DR_cx = (word) xorParams[2];
    r->DR_dx = (word) xorParams[3];
    r->DR_si = (word) xorX;
    r->DR_di = (word) xorY;
}

/* DR_EXIT: no region locked after */
void VidResetXOR(void)
{
    Unlock();
    xorHidden = FALSE;
    xorFlags = 0;
}
