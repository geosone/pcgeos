/*
 * winCreDest.c -- opening and closing windows (after winCreDest.asm:
 * WinOpen, AllocateWindow, WinRequestUnder, WinClose, WinDecRefCount).
 *
 * A window is opened on a parent window (or a GState of one) or, with
 * WPF_ROOT, on a video driver, whose strategy routine it keeps in
 * W_driverStrategy.  Its region (or the rectangle of the parameters)
 * is in pixels from the parent's origin.  The new window goes into the
 * tree by its priority and the tree is validated: what of it is
 * visible is washed out with its background and exposed.
 *
 * WinClose takes the window out of the tree, validates the tree (which
 * exposes what was under it) and drops its reference; the block is
 * freed when the last reference goes (WinDecRefCount).  Before, the
 * input and exposure objects' queues are flushed (WinFlushQueue,
 * MSG_META_OBJ_FLUSH_INPUT_QUEUE), each holding a reference until
 * MSG_META_WIN_DEC_REF_COUNT comes back from the end of it; without
 * either object, the owning process's queue (or the system input
 * object's).  So events queued with the handle find the closed window
 * (WRF_CLOSED, nothing visible) and not a freed handle.
 */
#include "winInt.h"

/* AllocateWindow: the block, its regions, the defaults; locked */
static WindowHandle AllocateWindow(GeodeHandle owner, word flags,
				   optr exposureObj)
{
    MemHandle wh = MemAlloc(sizeof(Window) + WINDOW_FREE_SPACE + 16,
			    HF_SWAPABLE | HF_SHARABLE,
			    HAF_ZERO_INIT | HAF_NO_ERR);
    Window *w;
    ChunkHandle *reg;
    word i;
    byte prio;

    if (owner)
	HandleModifyOwner(wh, owner);
    LMemInitHeap(wh, LMEM_TYPE_WINDOW, 0, sizeof(Window),
		 WIN_NUM_REGIONS + 1, WINDOW_FREE_SPACE);
    MemLock(wh);
    reg = &((Window *) MemDeref(wh))->W_winReg;
    for (i = 0; i < WIN_NUM_REGIONS; i++) {
	ChunkHandle ch = LMemAlloc(wh, 7 * sizeof(word));
	reg = &((Window *) MemDeref(wh))->W_winReg;
	reg[i] = ch;
	WinRegNull(wh, ch);
    }
    {
	ChunkHandle ch = LMemAlloc(wh, sizeof(word));
	((Window *) MemDeref(wh))->W_temp3Reg = ch;
	WinRegNull(wh, ch);
    }
    w = MemDeref(wh);
    w->W_exposureObj = exposureObj;
    w->W_TMatrix.TM_11.WWF_int = 1;
    w->W_TMatrix.TM_22.WWF_int = 1;
    prio = (byte) flags;
    if (!(prio & WPD_LAYER))
	prio |= LAYER_PRIO_STD << WPD_LAYER_OFFSET;
    if (!(prio & WPD_WIN))
	prio |= WIN_PRIO_STD;
    w->W_priority = prio;
    w->W_refCount = 1;
    return wh;
}

WindowHandle WinOpen(Handle parentWinOrVidDr, optr inputRecipient,
		     optr exposureRecipient, word colorFlags,
		     word redOrIndex, word green, word blue, word flags,
		     Handle layerID, GeodeHandle owner, const Region *winReg,
		     word axParam, word bxParam, word cxParam, word dxParam)
{
    WindowHandle wh = AllocateWindow(owner, flags, exposureRecipient),
		 parent = 0;
    Window *w, *p;
    sword params[4];
    DriverRegs r;

    WinTreeP();
    w = MemDeref(wh);
    w->W_layerID = (Handle) layerID;
    w->W_grFlags = 0;
    w->W_grRegFlags = 0;
    w->W_color = (WinColorFlags) colorFlags;
    w->W_colorRGB.RGB_red = (byte) redOrIndex;
    w->W_colorRGB.RGB_green = (byte) green;
    w->W_colorRGB.RGB_blue = (byte) blue;
    w->W_inputObj = inputRecipient;
    params[0] = (sword) axParam;
    params[1] = (sword) bxParam;
    params[2] = (sword) cxParam;
    params[3] = (sword) dxParam;
    WinRegSet(wh, w->W_winReg, winReg, params);
    w = MemDeref(wh);
    if (flags & WPF_ROOT) {
	DriverInfoStruct *dis = parentWinOrVidDr ?
				GeodeInfoDriver(parentWinOrVidDr) : NULL;
	w->W_driverStrategy = dis ? (void *) dis->DIS_strategy : NULL;
	w->W_parent = NULL_WINDOW;
	WinCalcWinBounds(wh);
    } else {
	LMemBlockHeader *b = MemLock(parentWinOrVidDr);
	parent = parentWinOrVidDr;
	if (b->LMBH_lmemType != LMEM_TYPE_WINDOW) {
	    parent = ((GState *) b)->GS_window;
	    MemUnlock(parentWinOrVidDr);
	    MemLock(parent);
	}
	p = MemDeref(parent);
	w->W_parent = parent;
	w->W_driverStrategy = p->W_driverStrategy;
	if (flags & WPF_SAVE_UNDER) {	/* WinRequestUnder */
	    memset(&r, 0, sizeof(r));
	    WinCalcWinBounds(wh);
	    w = MemDeref(wh);
	    r.DR_ax = (word) (w->W_winRect.R_right - w->W_winRect.R_left + 1);
	    r.DR_bx = (word) (w->W_winRect.R_bottom - w->W_winRect.R_top + 1);
	    if (WinCallVidDriver(w, DR_VID_REQUEST_UNDER, &r)) {
		MemUnlock(parent);	/* one has to go for it */
		WinClearSaveUnders(parent, (WinUnderFlags) r.DR_ax);
		MemLock(parent);
	    }
	}
	WinTInsert(wh, parent, (byte) flags, flags);
	w = MemDeref(wh);
	p = MemDeref(parent);
	GrMoveReg(WinReg(wh, w->W_winReg), p->W_winRect.R_left,
		  p->W_winRect.R_top);
	WinCalcWinBounds(wh);
	w = MemDeref(wh);
	w->W_ditherX = (word) w->W_winRect.R_left;
	w->W_ditherY = (word) w->W_winRect.R_top;
	w->W_pattPos = (word) (((w->W_winRect.R_top & 7) << 8) |
			       (w->W_winRect.R_left & 7));
	{				/* the areas over it go */
	    WinUnderFlags over;
	    MemUnlock(parent);
	    over = WinGetOverlyingSaveUnders(wh);
	    WinClearSaveUnders(wh, over);
	    MemLock(parent);
	    w = MemDeref(wh);
	}
	if (flags & WPF_SAVE_UNDER) {	/* vidcomUnder.c */
	    memset(&r, 0, sizeof(r));
	    r.DR_ax = 0x8000;
	    if (!WinCallVidDriver(w, DR_VID_SAVE_UNDER, &r))
		w->W_saveUnder = (WinUnderFlags) r.DR_ax;
	}
	MemUnlock(parent);
    }
    w = MemDeref(wh);
    if (flags & WPF_INIT_EXCLUDED)
	w->W_ptrFlags |= WPF_WIN_BRANCH_EXCLUDED;
    if (flags & WPF_INIT_SUSPENDED)
	w->W_suspendCount++;
    MemUnlock(wh);
    WinTValidateTree(wh);
    WinUnderOpened(wh);
    WinTreeV();
    return wh;
}

/* WinFlushQueue: a reference to the locked window wh until od's input
 * queues are flushed (none for a null od) */
void WinFlushQueue(WindowHandle wh, optr od)
{
    if (!OptrToHandle(od))
	return;
    ((Window *) MemDeref(wh))->W_refCount++;
    ObjStartQueueFlush(od, KMSG_META_WIN_DEC_REF_COUNT, &wh, sizeof(wh), wh);
}

void WinDecRefCount(WindowHandle win)
{
    Window *w;

    WinTreeP();
    w = MemLock(win);
    if (w && --w->W_refCount == 0) {
	MemUnlock(win);
	WinPtrFreed(win);
	MemFree(win);
    } else if (w)
	MemUnlock(win);
    WinTreeV();
}

void WinClose(WindowHandle win)
{
    WindowHandle wh = win, parent;
    LMemBlockHeader *b;
    Window *w;
    Boolean visible;
    WinUnderFlags under = 0;

    b = MemLock(win);
    if (!b)
	return;
    if (b->LMBH_lmemType != LMEM_TYPE_WINDOW) {	/* a GState */
	wh = ((GState *) b)->GS_window;
	MemUnlock(win);
	GrDestroyState(win);
	if (!wh)
	    return;
    } else
	MemUnlock(win);
    WinTreeP();
    w = MemLock(wh);
    parent = w->W_parent;
    visible = (Boolean) !WinRegIsNull(wh, w->W_univReg);
    MemUnlock(wh);
    WinPtrClosing(wh, parent);
    if (parent) {			/* save under areas */
	under = WinUnderClosing(wh, parent);
	if (!under)
	    WinUnderChange(wh);
	WinTRemove(wh);
    }
    w = MemLock(wh);
    WinRegNull(wh, w->W_univReg);
    WinRegNull(wh, ((Window *) MemDeref(wh))->W_visReg);
    WinRegNull(wh, ((Window *) MemDeref(wh))->W_maskReg);
    w = MemDeref(wh);
    w->W_grRegFlags &= (WinGrRegFlags) ~(WGRF_PATH_VALID |
					 WGRF_WIN_PATH_VALID);
    WinSetupMaskFlags(w);
    w->W_regFlags |= WRF_CLOSED;
    /* the queues first; the input object may yet get leaves */
    {
	optr in = w->W_inputObj, ex = w->W_exposureObj;
	WinFlushQueue(wh, in);
	w = MemDeref(wh);
	w->W_exposureObj = 0;
	if (ex != in)
	    WinFlushQueue(wh, ex);
	else if (!in) {
	    GeodeHandle proc = ObjOwnerProcess(wh);
	    WinFlushQueue(wh, proc ? ConstructOptr(proc, 0)
				   : WinPtrOutputOD());
	}
	w = MemDeref(wh);
    }
    w->W_parent = NULL_WINDOW;
    MemUnlock(wh);
    if (parent && visible)
	WinTValidateTree(parent);
    if (under)
	WinUnderClosed(wh, parent, under);
    WinTreeV();
    WinDecRefCount(wh);
}

/* ---------------------------------------------------------------------
 * Save under areas (WinRequestUnder and the save under parts of
 * WinOpen and WinClose; with winWindows.c: WinPerformObscure,
 * WinChangePrep, WinClearSaveUndersOverlappingParent, CorrectForSaveUnder,
 * and winIndividual.c: WinMaskOutSaveUnder).
 *
 * A window opened with WPF_SAVE_UNDER asks the video driver to save the
 * screen under it (DR_VID_SAVE_UNDER, vidcomUnder.c); the area's bit is
 * in its W_saveUnder.  Closing it puts the pixels back
 * (DR_VID_RESTORE_UNDER, within what of it was visible) instead of
 * having the windows below redraw.
 *
 * The asm left the regions of the windows below alone while the area
 * was up ("in hopes that it will soon go away") and cut it out of a
 * window only when drawing to it collided with the area.  The C window
 * system calculates the regions as always (the windows below lose the
 * area) and keeps the bits only for the exposure:
 *   - opening marks the windows its area overlaps (W_savedUnderMask and
 *     W_savedUnderFlags), the window's own branch not;
 *   - drawing to a marked window that touches the area (the driver's
 *     collision check), or invalidating there, clears its mask bit: the
 *     saved pixels no longer show what the window would;
 *   - closing restores the pixels, and validation does not wash out or
 *     expose the restored part of the windows whose mask bit is still
 *     set (WinCalcVisReg: the asm's (mask 1, flags 1), "we got lucky");
 *     the others are exposed as usual.  Then the bits go.
 * An area is given up (DR_VID_NUKE_UNDER, the bits cleared, nothing to
 * expose since the regions are right) when:
 *   - a window opens over it (not one of the siblings below the new
 *     window), or the driver needs its bit for a new area;
 *   - a window moves, is resized, changes priority or closes within the
 *     bounds of its parent that the area overlaps (as the asm, which
 *     says this may give up more areas than needed);
 *   - its own window moves, is resized or changes priority.
 * ------------------------------------------------------------------- */

WinUnderFlags winUnderClosing;		/* the area being closed */
optr winUnderRestored;			/* and its restored region */

/* after opening a window with an area: the windows under it marked */
void WinUnderOpened(WindowHandle wh)
{
    Window *w = MemLock(wh);
    WinUnderFlags f = w->W_saveUnder;
    DriverRegs r;
    Rectangle b;

    if (f) {
	memset(&r, 0, sizeof(r));
	r.DR_ax = f;
	(void) WinCallVidDriver(w, DR_VID_INFO_UNDER, &r);
	b.R_left = (sword) r.DR_ax;
	b.R_top = (sword) r.DR_bx;
	b.R_right = (sword) r.DR_cx;
	b.R_bottom = (sword) r.DR_dx;
	MemUnlock(wh);
	if (r.DR_di)
	    WinMarkUnderBits(WinRootOf(wh), wh, f, &b);
	return;
    }
    MemUnlock(wh);
}

/*
 * WinClose of a window with an area, still in the tree: the areas over
 * it given up, the pixels restored within
 * its visible region, which W_temp1Reg keeps for the validation of the
 * parent's tree (WinCalcVisReg).  Returns the area's bit.
 */
WinUnderFlags WinUnderClosing(WindowHandle wh, WindowHandle parent)
{
    Window *w = MemLock(wh);
    WinUnderFlags f = w->W_saveUnder;
    DriverRegs r;

    MemUnlock(wh);
    if (!f)
	return 0;
    WinClearSaveUnders(parent,
		       (WinUnderFlags) (WinGetOverlyingSaveUnders(wh) & ~f));
    w = MemLock(wh);
    WinRegCopy(WIN_REG(wh, w->W_visReg), WIN_REG(wh, w->W_temp1Reg));
    w = MemDeref(wh);
    memset(&r, 0, sizeof(r));
    r.DR_dsSi = WinReg(wh, w->W_temp1Reg);
    (void) WinCallVidDriver(w, DR_VID_RESTORE_UNDER, &r);
    w = MemDeref(wh);
    w->W_saveUnder = 0;
    winUnderClosing = f;
    winUnderRestored = WIN_REG(wh, w->W_temp1Reg);	/* stays locked */
    return f;
}

/* after the validation: the bits of the closed area go */
void WinUnderClosed(WindowHandle wh, WindowHandle parent, WinUnderFlags f)
{
    winUnderClosing = 0;
    winUnderRestored = 0;
    WinRegNull(wh, ((Window *) MemDeref(wh))->W_temp1Reg);
    MemUnlock(wh);
    WinClearUnderBits(WinRootOf(parent), f);
}
