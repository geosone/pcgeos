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
		     word layerID, GeodeHandle owner, const Region *winReg,
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
	    over = WinOverlyingUnders(wh);
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
