/*
 * vidcomEntry.c -- the strategy routine of the C video drivers (after
 * vidcomEntry.asm: DriverStrategy, VidStartExclusive, VidEndExclusive;
 * vidcomInfo.asm: VidInit, VidExit, VidTestDevice, VidSetDevice,
 * VidInfo, VidGetExclusive; vidcomExclBounds.asm: ExclRectBounds;
 * vidcomEscape.asm: VidEscape).
 *
 * As in DOS16:
 *   - the functions up to DR_VID_END_EXCLUSIVE run without the driver's
 *     semaphore, the others with it;
 *   - the drawing functions (after DR_VID_INFO_UNDER) of a GState other
 *     than the one with the exclusive are not done; their bounds are
 *     collected for DR_VID_END_EXCLUSIVE, which the caller invalidates;
 *   - the video functions fail until a device is set.
 * The device part shows what changed at the end of each function.
 *
 * Not yet: the save under areas, the XOR region, PUTSTRING; the escapes (all "not supported", di 0).  The functions do
 * nothing for now (the save under ones say "none").
 */
#include "vidcom.h"

#include <ghal.h>
#include <thread.h>
#include <sem.h>

static SemaphoreHandle videoSem;
static Handle exclusiveGState;
static Boolean exclusiveCausedAbort;
static Rectangle exclBound;

#define EXCL_MAX	((sword) 0x4000)	/* MAX_COORD */
#define EXCL_MIN	((sword) 0xc000)	/* MIN_COORD */

static sword Coord(dword v)
{
    return (sword) (word) v;
}

/* ExclRectBounds: the bounds of an operation not done */
static void ExclRectBounds(sword l, sword t, sword r, sword b,
			   const Window *w)
{
    if (l > r) { sword x = l; l = r; r = x; }
    if (t > b) { sword y = t; t = b; b = y; }
    if (w) {
	if (l < w->W_maskRect.R_left)
	    l = w->W_maskRect.R_left;
	if (t < w->W_maskRect.R_top)
	    t = w->W_maskRect.R_top;
	if (r > w->W_maskRect.R_right)
	    r = w->W_maskRect.R_right;
	if (b > w->W_maskRect.R_bottom)
	    b = w->W_maskRect.R_bottom;
    }
    if (l > r || t > b)
	return;
    if (l < exclBound.R_left)
	exclBound.R_left = l;
    if (t < exclBound.R_top)
	exclBound.R_top = t;
    if (r > exclBound.R_right)
	exclBound.R_right = r;
    if (b > exclBound.R_bottom)
	exclBound.R_bottom = b;
}

static void ExclBounds(word function, DriverRegs *r)
{
    switch (function) {
    case DR_VID_RECT:
	ExclRectBounds(Coord(r->DR_ax), Coord(r->DR_bx), Coord(r->DR_cx),
		       Coord(r->DR_dx), r->DR_esDi);
	break;
    case DR_VID_LINE:			/* ExclLineBounds */
	ExclRectBounds(Coord(r->DR_ax), Coord(r->DR_bx), Coord(r->DR_cx),
		       Coord(r->DR_dx), r->DR_esDi);
	break;
    case DR_VID_MOVBITS:		/* ExclMovBitsBounds: the destination */
	ExclRectBounds(Coord(r->DR_cx), Coord(r->DR_dx),
		       (sword) (Coord(r->DR_cx) + Coord(r->DR_si) - 1),
		       (sword) (Coord(r->DR_dx) + Coord(r->DR_bp) - 1),
		       r->DR_esDi);
	break;
    case DR_VID_PUTBITS:		/* ExclPutBitsBounds */
	if (r->DR_bpPtr) {
	    const PutBitsArgs *pba = r->DR_bpPtr;
	    ExclRectBounds(Coord(r->DR_ax), Coord(r->DR_bx),
			   (sword) (Coord(r->DR_ax) + pba->PBA_bm.B_width - 1),
			   (sword) (Coord(r->DR_bx) + pba->PBA_bm.B_height - 1),
			   r->DR_esDi);
	}
	break;
    case DR_VID_PUTLINE:
	ExclRectBounds(Coord(r->DR_ax), Coord(r->DR_bx), Coord(r->DR_cx),
		       Coord(r->DR_dx), r->DR_esDi);
	break;
    case DR_VID_POLYGON:		/* ExclPolygonBounds */
	if (r->DR_farPtr && (word) r->DR_cx) {
	    Rectangle b;
	    VidPointBounds(r->DR_farPtr, (word) r->DR_cx, &b);
	    ExclRectBounds(b.R_left, b.R_top, b.R_right, b.R_bottom,
			   r->DR_esDi);
	}
	break;
    case DR_VID_POLYLINE:		/* ExclPolylineBounds */
	if (r->DR_farPtr) {
	    Rectangle b;
	    sword bw = (sword) (byte) r->DR_ax;
	    sword bh = (sword) (byte) (r->DR_ax >> 8);
	    if (VidPolylineBounds(r->DR_farPtr, bw < 1 ? 1 : bw,
				  bh < 1 ? 1 : bh, &b))
		ExclRectBounds(b.R_left, b.R_top, b.R_right, b.R_bottom,
			       r->DR_esDi);
	}
	break;
    case DR_VID_REGION:			/* ExclRegionBounds */
	{
	    Rectangle b;
	    if (VidRegionBounds(r, &b))
		ExclRectBounds(b.R_left, b.R_top, b.R_right, b.R_bottom,
			       r->DR_esDi);
	}
	break;
    case DR_VID_DASH_LINE:		/* ExclDashBounds */
    case DR_VID_DASH_FILL:
	if (r->DR_farPtr) {
	    const DashInfo *di = r->DR_farPtr;
	    sword ddx = 0, ddy = 0;
	    if (function == DR_VID_DASH_FILL) {
		ddx = Coord(r->DR_ax);
		ddy = Coord(r->DR_cx);
	    }
	    ExclRectBounds(di->DI_pt1.P_x, di->DI_pt1.P_y, di->DI_pt2.P_x,
			   di->DI_pt2.P_y, r->DR_esDi);
	    ExclRectBounds((sword) (di->DI_pt1.P_x + ddx),
			   (sword) (di->DI_pt1.P_y + ddy),
			   (sword) (di->DI_pt2.P_x + ddx),
			   (sword) (di->DI_pt2.P_y + ddy), r->DR_esDi);
	}
	break;
    default:
	/* PUTSTRING: not there yet */
	break;
    }
}

static void VidStartExclusive(Handle gstate)
{
    ghal_crit_enter();
    exclusiveGState = gstate;
    exclBound.R_left = exclBound.R_top = EXCL_MAX;
    exclBound.R_right = exclBound.R_bottom = EXCL_MIN;
    ghal_crit_leave();
}

static void VidEndExclusive(DriverRegs *r)
{
    ghal_crit_enter();
    r->DR_ax = 0;
    if (exclusiveGState == (Handle) r->DR_bx) {
	exclusiveGState = 0;
	if (exclusiveCausedAbort) {
	    exclusiveCausedAbort = FALSE;
	    r->DR_ax = 1;
	    r->DR_si = (word) exclBound.R_left;
	    r->DR_di = (word) exclBound.R_top;
	    r->DR_cx = (word) exclBound.R_right;
	    r->DR_dx = (word) exclBound.R_bottom;
	}
    }
    ghal_crit_leave();
}

/* the other GState has the exclusive: not done */
static Boolean Excluded(word function, DriverRegs *r)
{
    const GState *gs = r->DR_dsSi;
    Boolean out = FALSE;

    ghal_crit_enter();
    if (exclusiveGState && (!gs ||
			    gs->GS_header.LMBH_handle != exclusiveGState)) {
	exclusiveCausedAbort = TRUE;
	ExclBounds(function, r);
	out = TRUE;
    }
    ghal_crit_leave();
    return out;
}

static Boolean VidInit(void)
{
    if (!videoSem)
	videoSem = ThreadAllocSem(1);
    return VidDevInit();
}

static void VidExit(void)
{
    VidDevExit();
    DriverTable.VDI_device = VID_NO_DEVICE;
    if (videoSem)
	ThreadFreeSem(videoSem);
    videoSem = 0;
}

static Boolean VidSetDevice(DriverRegs *r)
{
    if (VidDevSet(r->DR_dsSi)) {
	r->DR_ax = (word) DP_INVALID_DEVICE;
	return TRUE;
    }
    DriverTable.VDI_device = 0;
    VidDevSetPtr(NULL);			/* start hidden, with the default */
    r->DR_ax = DP_PRESENT;
    return FALSE;
}

/* the functions with the semaphore */
static Boolean VidCall(word function, DriverRegs *r)
{
    switch (function) {
    case DR_VID_GETPIXEL: {
	dword p = VidDevGetPixel(Coord(r->DR_ax), Coord(r->DR_bx));
	r->DR_ax = (byte) (p >> 16);		/* ah raw: 0 */
	r->DR_bx = (byte) (p >> 8) | (word) ((byte) p << 8);
	return FALSE;
    }
    case DR_VID_SETPTR:
	VidDevSetPtr(r->DR_dsSi);
	return FALSE;
    case DR_VID_HIDEPTR:
	VidDevHidePtr();
	return FALSE;
    case DR_VID_SHOWPTR:
	VidDevShowPtr();
	return FALSE;
    case DR_VID_MOVEPTR:
	VidDevMovePtr(Coord(r->DR_ax), Coord(r->DR_bx));
	r->DR_ax = 0;				/* no save under hit */
	return FALSE;
    case DR_VID_SAVE_UNDER:
	return TRUE;				/* not possible */
    case DR_VID_REQUEST_UNDER:
	return FALSE;				/* none to remove */
    case DR_VID_CHECK_UNDER:
	r->DR_ax = 0;
	return FALSE;
    case DR_VID_INFO_UNDER:
	r->DR_di = 0;
	return FALSE;
    case DR_VID_CLEAR_XOR:
	r->DR_ax = r->DR_bx = r->DR_cx = r->DR_dx = 0;
	r->DR_si = r->DR_di = 0;
	return FALSE;
    case DR_VID_RECT:
	VidDrawRect(r);
	return FALSE;
    case DR_VID_LINE:
	VidDrawLine(r);
	return FALSE;
    case DR_VID_GETBITS:
	VidGetBits(r);
	return FALSE;
    case DR_VID_PUTBITS:
	VidPutBits(r);
	return FALSE;
    case DR_VID_MOVBITS:
	VidMovBits(r);
	return FALSE;
    case DR_VID_REGION:
	VidDrawRegion(r);
	return FALSE;
    case DR_VID_PUTLINE:
	VidPutLine(r);
	return FALSE;
    case DR_VID_POLYGON:
	VidPolygon(r);
	return FALSE;
    case DR_VID_POLYLINE:
	VidPolyline(r);
	return FALSE;
    case DR_VID_DASH_LINE:
	VidDashLine(r);
	return FALSE;
    case DR_VID_DASH_FILL:
	VidDashFill(r);
	return FALSE;
    case DR_VID_SCREEN_ON:
	VidDevScreen(TRUE);
	return FALSE;
    case DR_VID_SCREEN_OFF:
	VidDevScreen(FALSE);
	return FALSE;
    case DR_VID_SET_PALETTE:
	VidSetPalette(r);
	return FALSE;
    case DR_VID_GET_PALETTE:
	VidGetPalette(r);
	return FALSE;
    default:
	return FALSE;
    }
}

Boolean VidStrategy(word function, DriverRegs *r)
{
    Boolean carry;

    switch (function) {
    case DR_INIT:
	return VidInit();
    case DR_EXIT:
	VidExit();
	return FALSE;
    case DR_SUSPEND:
    case DR_UNSUSPEND:
	return FALSE;
    case DRE_TEST_DEVICE:
	r->DR_ax = VidDevTest(r->DR_dsSi);
	return FALSE;
    case DRE_SET_DEVICE:
	return VidSetDevice(r);
    }
    if (function & 0x8000) {		/* VidEscape: none supported */
	r->DR_di = 0;
	return FALSE;
    }
    if (DriverTable.VDI_device == VID_NO_DEVICE ||
	function > DR_VID_LAST_FUNCTION)
	return TRUE;
    switch (function) {
    case DR_VID_INFO:			/* dx:si */
	r->DR_dsSi = &DriverTable;
	return FALSE;
    case DR_VID_GET_EXCLUSIVE:
	r->DR_bx = exclusiveGState;
	return FALSE;
    case DR_VID_START_EXCLUSIVE:
	VidStartExclusive((Handle) r->DR_bx);
	return FALSE;
    case DR_VID_END_EXCLUSIVE:
	VidEndExclusive(r);
	return FALSE;
    }
    if (function > DR_VID_LAST_NON_EXCLUSIVE && Excluded(function, r))
	return FALSE;
    ThreadPSem(videoSem);
    carry = VidCall(function, r);
    VidDevUpdate();
    ThreadVSem(videoSem);
    return carry;
}
