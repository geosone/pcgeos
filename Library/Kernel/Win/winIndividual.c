/*
 * winIndividual.c -- routines of the window system that work on one
 * window (after winIndividual.asm: WinAckUpdate, WinSuspendUpdate,
 * WinUnSuspendUpdate, WinInvalTree, WinSendUpdateComplete,
 * GrBeginUpdate, GrEndUpdate, WinInvalReg, WinCalcVisReg,
 * WinSendExpEvent, WinWashOut, WashOutInval, WinValWinStruct,
 * SetupMaskFlags, WinValClipLine, WinAddToInvalReg).
 *
 * What becomes visible of a window is invalid (W_invalReg): it is
 * washed out with the window's background (unless WCF_TRANSPARENT) and
 * the exposure object gets MSG_META_EXPOSED, one at a time
 * (WRF_EXPOSE_PENDING) and not while the update is suspended.  A
 * WCF_PLAIN window gets none; its invalid region is dropped after the
 * wash.  GrBeginUpdate makes the invalid region the update region, to
 * which the update GState draws; GrEndUpdate ends it.
 *
 * The mask the video driver draws through (W_maskReg) is the visible
 * region without the invalid one, for the update GState within the
 * update region (WinValWinStruct), and within the clip paths of the
 * GState (GS_clipPath, GS_winClipPath: GrClipMask).
 *
 * A WCF_MASKED window (a disabled one, darkened by the specific UI) is
 * drawn by the video driver through a 50% checker; washing it out also
 * draws the other half black (WCF_DRAW_MASK), even if it is
 * transparent (WashOutInval).
 */
#include "winInt.h"

#define CLIP_TOP_START	((sword) 0xc000)
#define CLIP_PAST_END	0x3fff

/*
 * WinValClipLine: make the clip line the band of W_maskReg that holds
 * line y.  W_clipRect top/bottom are the lines of the band, left/right
 * the first on and the last off point (EOREGREC when the band is
 * empty), W_clipPtr the offset of its pairs in the chunk;
 * WGF_CLIP_SIMPLE when it has one pair, WGF_CLIP_NULL when none.
 *
 * The asm searched from the current band, back or on; the region is
 * walked from its start here, which gives the same band.
 */
void WinValClipLine(Window *w, sword y)
{
    sword *reg = WinRegionOf(w, w->W_maskReg), *p = reg, *line;
    sword top = CLIP_TOP_START, bottom, right;

    w->W_grFlags &= (WinGrFlags) ~(WGF_CLIP_SIMPLE | WGF_CLIP_NULL);
    for (;;) {
	if ((word) *p == EOREGREC) {	/* past the end: empty to the end */
	    bottom = CLIP_PAST_END;
	    line = p;
	    break;
	}
	if (*p >= y) {
	    bottom = *p;
	    line = p + 1;
	    break;
	}
	top = (sword) (*p + 1);
	for (p++; (word) *p != EOREGREC; p++)
	    ;
	p++;
    }
    w->W_clipRect.R_top = top;
    w->W_clipRect.R_bottom = bottom;
    w->W_clipPtr = (word) ((byte *) line - (byte *) reg);
    if ((word) line[0] == EOREGREC) {
	w->W_clipRect.R_left = w->W_clipRect.R_right = (sword) EOREGREC;
	w->W_grFlags |= WGF_CLIP_NULL;
	return;
    }
    w->W_clipRect.R_left = line[0];
    right = line[1];
    if ((word) line[2] == EOREGREC)
	w->W_grFlags |= WGF_CLIP_SIMPLE;
    else
	for (p = line + 2; (word) *p != EOREGREC; p += 2)
	    right = p[1];
    w->W_clipRect.R_right = right;
}

/* ---------------------------------------------------------------------
 * The mask
 * ------------------------------------------------------------------- */

/* SetupMaskFlags */
void WinSetupMaskFlags(Window *w)
{
    const sword *m = WinRegionOf(w, w->W_maskReg);
    Rectangle b;

    GrGetPtrRegBounds((const Region *) m, &b);
    w->W_maskRect = b;
    w->W_grFlags = (WinGrFlags) ((w->W_grFlags & WGF_XFORM_VALID) |
				 WGF_MASK_VALID);
    w->W_clipRect.R_top = w->W_clipRect.R_bottom = (sword) EOREGREC;
    if ((word) m[0] == EOREGREC) {
	w->W_grFlags |= WGF_MASK_NULL;
	return;
    }
    if ((word) m[5] == EOREGREC && (word) m[6] == EOREGREC) {
	w->W_grFlags |= WGF_MASK_SIMPLE | WGF_CLIP_SIMPLE;
	w->W_clipRect = b;
	w->W_clipPtr = 3 * sizeof(word);
    }
}

/*
 * WinValWinStruct: the window ready for drawing with GState gh (gs
 * locked): its matrix composed and its mask calculated, if they are
 * not valid for this GState.
 */
void WinValWinStruct(GStateHandle gh, GState *gs, WindowHandle wh)
{
    Window *w = MemDeref(wh);

    if (w->W_curState != gh) {
	w->W_grFlags &= (WinGrFlags) ~(WGF_XFORM_VALID | WGF_MASK_VALID);
	w->W_grRegFlags &= (WinGrRegFlags) ~(WGRF_PATH_VALID |
					     WGRF_WIN_PATH_VALID);
    }
    if (!(w->W_grFlags & WGF_XFORM_VALID))
	GrComposeMatrix(gh, gs, w);
    w = MemDeref(wh);
    if (!(w->W_grFlags & WGF_MASK_VALID)) {
	optr mask = WIN_REG(wh, w->W_maskReg);
	WinRegSub(WIN_REG(wh, w->W_visReg), WIN_REG(wh, w->W_invalReg), mask,
		  WIN_REG(wh, w->W_temp2Reg));
	w = MemDeref(wh);
	if (w->W_updateState == gh)
	    WinRegAnd(mask, WIN_REG(wh, w->W_updateReg), mask);
	w = MemDeref(wh);
	if (gs->GS_clipPath || gs->GS_winClipPath)
	    GrClipMask(gh, wh, w->W_maskReg);
	WinRegSmall(wh, ((Window *) MemDeref(wh))->W_temp2Reg);
	w = MemDeref(wh);
	w->W_grRegFlags |= WGRF_PATH_VALID | WGRF_WIN_PATH_VALID;
    }
    WinSetupMaskFlags(w);
    w->W_curState = gh;
}

/* ---------------------------------------------------------------------
 * Washing out and exposing
 * ------------------------------------------------------------------- */

/* WashOutInval: fill region reg of the window with its background */
void WinWashOutReg(WindowHandle wh, ChunkHandle reg)
{
    Window *w = MemDeref(wh);
    ChunkHandle mask;
    WinGrFlags flags;
    Rectangle maskRect;
    GStateHandle gh;
    GState *gs;
    DriverRegs r;

    if ((word) *WinReg(wh, reg) == EOREGREC ||
	(w->W_regFlags & WRF_CLOSED) ||
	(w->W_color & (WCF_TRANSPARENT | WCF_MASKED)) == WCF_TRANSPARENT)
	return;
    mask = w->W_maskReg;		/* SwapInvalMask */
    w->W_maskReg = reg;
    flags = w->W_grFlags;
    maskRect = w->W_maskRect;
    WinSetupMaskFlags(w);
    gh = GrCreateState(wh);
    GrSetAreaColorMap(gh, (word) (w->W_color & WCF_MAP_MODE));
    if (w->W_color & WCF_RGB)
	GrSetAreaColor(gh, CF_RGB, w->W_colorRGB.RGB_red,
		       w->W_colorRGB.RGB_green, w->W_colorRGB.RGB_blue);
    else
	GrSetAreaColor(gh, CF_INDEX, w->W_colorRGB.RGB_red, 0, 0);
    gs = MemLock(gh);
    w = MemDeref(wh);
    memset(&r, 0, sizeof(r));
    r.DR_ax = (word) w->W_maskRect.R_left;
    r.DR_bx = (word) w->W_maskRect.R_top;
    r.DR_cx = (word) w->W_maskRect.R_right;
    r.DR_dx = (word) w->W_maskRect.R_bottom;
    r.DR_dsSi = gs;
    r.DR_si = (word) offsetof(GState, GS_areaAttr);
    if (!(w->W_color & WCF_TRANSPARENT))
	WinCallVidDriver(w, DR_VID_RECT, &r);
    if (w->W_color & WCF_MASKED) {	/* the mask: the other half, black */
	MemUnlock(gh);
	GrSetAreaColor(gh, CF_INDEX, C_BLACK, 0, 0);
	gs = MemLock(gh);
	w = MemDeref(wh);
	r.DR_dsSi = gs;
	w->W_color |= WCF_DRAW_MASK;
	WinCallVidDriver(w, DR_VID_RECT, &r);
	w->W_color &= (WinColorFlags) ~WCF_DRAW_MASK;
    }
    gs->GS_window = 0;
    MemUnlock(gh);
    GrDestroyState(gh);
    w = MemDeref(wh);
    w->W_maskReg = mask;
    w->W_grFlags = flags;
    w->W_maskRect = maskRect;
    w->W_clipRect.R_top = w->W_clipRect.R_bottom = (sword) EOREGREC;
}

/* WinSendExpEvent */
void WinSendExpEvent(WindowHandle wh)
{
    Window *w = MemDeref(wh);

    if ((w->W_color & WCF_PLAIN) || w->W_updateState ||
	(w->W_regFlags & WRF_EXPOSE_PENDING) || w->W_suspendCount ||
	WinRegIsNull(wh, w->W_visReg) || WinRegIsNull(wh, w->W_invalReg) ||
	!OptrToHandle(w->W_exposureObj))
	return;
    if (!(w->W_regFlags & WRF_CLOSED))
	CObjMessageP(MF_FORCE_QUEUE, w->W_exposureObj, KMSG_META_EXPOSED,
		     &wh, sizeof(wh), NULL, 0);
    w->W_regFlags |= WRF_EXPOSE_PENDING;
}

/* WinSendUpdateComplete */
void WinSendUpdateComplete(WindowHandle wh)
{
    Window *w = MemDeref(wh);

    if (!(w->W_regFlags & WRF_CLOSED) && OptrToHandle(w->W_inputObj))
	CObjMessageP(MF_FORCE_QUEUE, w->W_inputObj,
		     KMSG_META_WIN_UPDATE_COMPLETE, &wh, sizeof(wh), NULL, 0);
}

/*
 * WinAddToInvalReg: W_temp1Reg (screen) within the visible region to
 * the invalid region; that part washed out, an exposure sent.
 */
void WinAddToInvalReg(WindowHandle wh)
{
    Window *w = MemDeref(wh);
    optr add;

    if (w->W_regFlags & WRF_CLOSED)
	return;
    add = WIN_REG(wh, w->W_temp3Reg);
    WinRegAnd(WIN_REG(wh, w->W_temp1Reg), WIN_REG(wh, w->W_visReg), add);
    w = MemDeref(wh);
    if ((word) *(word *) LMemDeref(add) != EOREGREC) {
	WinRegOr(WIN_REG(wh, w->W_invalReg), add,
		 WIN_REG(wh, w->W_invalReg));
	w = MemDeref(wh);
	WinWashOutReg(wh, w->W_temp3Reg);
	w = MemDeref(wh);
	if (w->W_color & WCF_PLAIN)
	    WinRegNull(wh, w->W_invalReg);
	WinSendExpEvent(wh);
	w = MemDeref(wh);
	w->W_grFlags &= (WinGrFlags) ~WGF_MASK_VALID;
    }
    WinRegSmall(wh, w->W_temp1Reg);
    w = MemDeref(wh);
    WinRegSmall(wh, w->W_temp3Reg);
}

/*
 * WinCalcVisReg: W_visReg = W_univReg - W_childReg; what is new of it
 * to the invalid region.
 */
void WinCalcVisReg(WindowHandle wh)
{
    Window *w = MemDeref(wh);

    /* W_temp1Reg = NOT the old visible region */
    WinRegNot(WIN_REG(wh, w->W_visReg), WIN_REG(wh, w->W_temp1Reg));
    w = MemDeref(wh);
    WinRegSub(WIN_REG(wh, w->W_univReg), WIN_REG(wh, w->W_childReg),
	      WIN_REG(wh, w->W_visReg), WIN_REG(wh, w->W_temp2Reg));
    w = MemDeref(wh);
    WinRegSmall(wh, w->W_temp2Reg);
    w = MemDeref(wh);
    w->W_grFlags &= (WinGrFlags) ~WGF_MASK_VALID;
    w->W_grRegFlags &= (WinGrRegFlags) ~(WGRF_PATH_VALID |
					 WGRF_WIN_PATH_VALID);
    WinAddToInvalReg(wh);
}

/* ---------------------------------------------------------------------
 * The API
 * ------------------------------------------------------------------- */

static Window *Enter(Handle h, WindowHandle *wh)
{
    Window *w;

    WinTreeP();
    w = WinLockFromHandle(h, wh);
    if (!w)
	WinTreeV();
    return w;
}

static void Leave(WindowHandle wh)
{
    MemUnlock(wh);
    WinTreeV();
}

void WinAckUpdate(WindowHandle win)
{
    WindowHandle wh;
    Window *w = Enter(win, &wh);

    if (!w)
	return;
    w->W_regFlags &= (WinRegFlags) ~WRF_EXPOSE_PENDING;
    w->W_grFlags &= (WinGrFlags) ~WGF_MASK_VALID;
    WinRegNull(wh, w->W_invalReg);
    WinSendUpdateComplete(wh);
    Leave(wh);
}

void WinSuspendUpdate(WindowHandle win)
{
    WindowHandle wh;
    Window *w = Enter(win, &wh);

    if (!w)
	return;
    w->W_suspendCount++;
    Leave(wh);
}

void WinUnSuspendUpdate(WindowHandle win)
{
    WindowHandle wh;
    Window *w = Enter(win, &wh);

    if (!w)
	return;
    if (w->W_suspendCount && --w->W_suspendCount == 0)
	WinSendExpEvent(wh);
    Leave(wh);
}

/* WinInvalReg: reg (or the rectangle of the parameters) in window
 * coordinates */
void WinInvalReg(WindowHandle win, const Region *reg, word axParam,
		 word bxParam, word cxParam, word dxParam)
{
    WindowHandle wh;
    Window *w = Enter(win, &wh);
    sword params[4];

    if (!w)
	return;
    params[0] = (sword) axParam;
    params[1] = (sword) bxParam;
    params[2] = (sword) cxParam;
    params[3] = (sword) dxParam;
    WinRegSet(wh, w->W_temp1Reg, reg, params);	/* SetupOtherReg */
    w = MemDeref(wh);
    GrMoveReg(WinReg(wh, w->W_temp1Reg), w->W_winRect.R_left,
	      w->W_winRect.R_top);
    WinAddToInvalReg(wh);
    Leave(wh);
}

/* WinInvalTreeHere: reg in screen coordinates, the window and all
 * below it */
static void InvalTreeHere(WindowHandle wh, const Region *reg,
			  const sword *params)
{
    Window *w = MemLock(wh);
    WindowHandle ch, next;

    if (!(w->W_regFlags & WRF_CLOSED)) {
	WinRegSet(wh, w->W_temp1Reg, reg, params);
	w = MemDeref(wh);
	w->W_regFlags |= WRF_INVAL_TREE;
	WinAddToInvalReg(wh);
	w = MemDeref(wh);
	w->W_regFlags &= (WinRegFlags) ~WRF_INVAL_TREE;
	for (ch = w->W_firstChild; ch; ch = next) {
	    Window *c = MemLock(ch);
	    next = c->W_nextSibling;
	    MemUnlock(ch);
	    InvalTreeHere(ch, reg, params);
	}
    }
    MemUnlock(wh);
}

/* WinInvalTree (not in the C API of win.h): screen coordinates */
void WinInvalTree(Handle win, const Region *reg, word axParam, word bxParam,
		  word cxParam, word dxParam)
{
    WindowHandle wh;
    Window *w = Enter(win, &wh);
    sword params[4];

    if (!w)
	return;
    params[0] = (sword) axParam;
    params[1] = (sword) bxParam;
    params[2] = (sword) cxParam;
    params[3] = (sword) dxParam;
    MemUnlock(wh);
    InvalTreeHere(wh, reg, params);
    WinTreeV();
}

void GrBeginUpdate(GStateHandle gstate)
{
    WindowHandle wh;
    Window *w = Enter(gstate, &wh);
    ChunkHandle t;

    if (!w)
	return;
    w->W_updateState = gstate;
    if (!w->W_suspendCount) {		/* else a null update region */
	t = w->W_invalReg;
	w->W_invalReg = w->W_updateReg;
	w->W_updateReg = t;
    }
    w->W_regFlags &= (WinRegFlags) ~WRF_EXPOSE_PENDING;
    if (!(w->W_regFlags & WRF_CLOSED))
	w->W_grFlags &= (WinGrFlags) ~WGF_MASK_VALID;
    Leave(wh);
}

void GrEndUpdate(GStateHandle gstate)
{
    WindowHandle wh;
    Window *w = Enter(gstate, &wh);

    if (!w)
	return;
    w->W_updateState = 0;
    WinRegNull(wh, w->W_updateReg);
    w = MemDeref(wh);
    if (!(w->W_regFlags & WRF_CLOSED)) {
	w->W_grFlags &= (WinGrFlags) ~WGF_MASK_VALID;
	WinSendUpdateComplete(wh);
	WinSendExpEvent(wh);
    }
    Leave(wh);
}
