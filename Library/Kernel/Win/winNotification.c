/*
 * winNotification.c -- the window under the pointer (after
 * winNotification.asm: WinMovePtr, WinCheckPtr, WinSendWinChange,
 * WinGrabChange, WinReleaseChange, WinEnsureChangeNotification,
 * WinChangePtrNotification, WinForceWinChange, WinRedoMovePtr,
 * WinChangeAck, WinCommonTraverse, WinCommonEnd, UpdatePtrImages,
 * GetImpliedOD, GetFinalWinAndGeode, ActivateWinPtrImages,
 * ActivateGeodePtrImage, WinTraverseHere, CheckHere, CheckInChildren,
 * CheckAcross, CheckInUniv, CheckInVis, LowCheckInVis,
 * SendEnterLeaveEvent, SendImpliedChangeEvent, WinProcessEventQueue,
 * WinFlushSendQueue, WinSetPtrImage).
 *
 * The input manager reports each move of the pointer (WinMovePtr).  As
 * long as the pointer stays in the rectangle of the visible region it
 * was last found in, nothing happens.  Otherwise the change object (the
 * UI's flow object, WinGrabChange) gets MSG_META_WIN_CHANGE and answers
 * with WinChangeAck: the tree is walked from the window the pointer was
 * in to where it is now.  On the way:
 *   - each window whose universe the pointer enters or leaves gets
 *     MSG_META_RAW_UNIV_ENTER or MSG_META_RAW_UNIV_LEAVE (WPF_PTR_IN_UNIV);
 *   - before the first enter, MSG_META_IMPLIED_WIN_CHANGE names the
 *     window the pointer is in at the end (the implied window) and its
 *     input object;
 *   - the pointer images of the gadget and window levels follow the
 *     implied window (or the active window of its geode), the geode
 *     level its geode (winGeode.c).
 * The messages are collected while the tree is locked and go to the
 * change object after it is released, in order.  A message's parameters
 * are WinPtrEventParams (the input object, the window); goc's
 * prototype of MSG_META_IMPLIED_WIN_CHANGE has the same layout.
 * MSG_META_WIN_CHANGE carries WinChangeParams.
 *
 * Opening, closing, moving windows (any change of a visible region,
 * WinCalcVisReg) and crossing into another save under area make the
 * next move send MSG_META_WIN_CHANGE again; so does a button or key
 * (WinEnsureChangeNotification) while one is outstanding.
 *
 * DOS16 builds without WIN_ENTER_LEAVE_CONTROL (WinMouseGrab and the
 * other grab and exclude routines, MSG_META_UNIV_ENTER and the other
 * filtered messages) and without WIN_VIS_ENTER_LEAVE
 * (MSG_META_RAW_VIS_*); so does this.
 *
 * Differences from the asm:
 *   - the messages wait in arrays, not in two event queues;
 *   - without a change object, nothing is counted as outstanding (the
 *     asm counted a MSG_META_WIN_CHANGE sent to nobody, and the pointer
 *     was then never tested again);
 *   - before the input manager has reported a position, a change of a
 *     window sends nothing (the asm sent changes for tree 0, and each
 *     answer sent the next one);
 *   - a window is freed when it closes unless a GState still has it:
 *     closing sends MSG_META_RAW_UNIV_LEAVE to the windows of its branch
 *     the pointer was in, and the walk starts from its parent
 *     (WinPtrClosing).  The asm kept the window until the walk had left
 *     it.
 */
#include "winInt.h"

#include <stdlib.h>

#define WCF_IMPLIED_CHANGE_SENT	0x01
#define WCF_FLUSHING_QUEUE	0x02

typedef struct {
    Message		PE_msg;
    optr		PE_od;
    WindowHandle	PE_win;
} PtrEvent;

typedef struct {
    PtrEvent	*PQ_events;
    word	PQ_head, PQ_count, PQ_size;
} PtrQueue;

/* the variables (winVariable.def); those of WinMovePtr under
 * ghal_crit_enter (the asm's INT_OFF), the others under the tree lock,
 * the pointer images also under ptrImageSem */
static struct {
    optr		outputOD;	/* wPtrOutputOD */
    word		changeCount;	/* MSG_META_WIN_CHANGEs outstanding */
    Rectangle		testBounds;	/* valid if changeCount is 0 */
    sword		locX, locY;	/* at WinMovePtr */
    WindowHandle	treeAtMove;
    WinUnderFlags	suAtMove;
    byte		changeFlags;	/* WCF_* */
    sword		lastX, lastY;	/* wPtrLastLoc */
    WindowHandle	lastTree;
    WindowHandle	impliedWin;
    GeodeHandle		impliedWinProc;
    optr		impliedWinOD;
    WindowHandle	locatedVisWin;
    WindowHandle	finalWin;	/* whose images are shown */
    GeodeHandle		finalGeode;
    GeodeHandle		activeGeode;	/* WinSysSetActiveGeode */
} wp;

static PtrQueue eventQueue, sendQueue;
static KSem ptrImageSem = { 1, 0 };

void WinPtrImageP(void)
{
    KSemP(&ptrImageSem, KSEM_FOREVER);
}

void WinPtrImageV(void)
{
    KSemV(&ptrImageSem);
}

/* ---------------------------------------------------------------------
 * The message queues
 * ------------------------------------------------------------------- */

static void Put(PtrQueue *q, Message msg, optr od, WindowHandle win)
{
    PtrEvent *e;

    if (q->PQ_head + q->PQ_count == q->PQ_size) {
	if (q->PQ_head) {
	    memmove(q->PQ_events, q->PQ_events + q->PQ_head,
		    q->PQ_count * sizeof(PtrEvent));
	    q->PQ_head = 0;
	} else {
	    word n = (word) (q->PQ_size ? q->PQ_size * 2 : 16);
	    PtrEvent *p = realloc(q->PQ_events, n * sizeof(PtrEvent));
	    if (!p)
		return;			/* out of memory: dropped */
	    q->PQ_events = p;
	    q->PQ_size = n;
	}
    }
    e = &q->PQ_events[q->PQ_head + q->PQ_count++];
    e->PE_msg = msg;
    e->PE_od = od;
    e->PE_win = win;
}

static Boolean Get(PtrQueue *q, PtrEvent *e)
{
    if (!q->PQ_count)
	return FALSE;
    *e = q->PQ_events[q->PQ_head++];
    if (!--q->PQ_count)
	q->PQ_head = 0;
    return TRUE;
}

/* WinSendViaPtrEventQueue */
static void Send(Message msg, optr od, WindowHandle win)
{
    Put(&eventQueue, msg, od, win);
}

/* SendEnterLeaveEvent */
static void SendEnterLeave(Window *w, Message msg)
{
    Send(msg, w->W_inputObj, w->W_header.LMBH_handle);
}

/* SendImpliedChangeEvent: once per walk, before the first enter; its
 * parameters are filled in at the end (WinProcessEventQueue) */
static void SendImpliedChange(void)
{
    if (wp.changeFlags & WCF_IMPLIED_CHANGE_SENT)
	return;
    Send(KMSG_META_IMPLIED_WIN_CHANGE, 0, 0);
    wp.changeFlags |= WCF_IMPLIED_CHANGE_SENT;
}

/* WinProcessEventQueue: the walk's messages to the send queue, the
 * implied window filled in */
static void ProcessEventQueue(optr od, WindowHandle win)
{
    PtrEvent e;

    ghal_crit_enter();
    while (Get(&eventQueue, &e)) {
	if (e.PE_msg == KMSG_META_IMPLIED_WIN_CHANGE) {
	    e.PE_od = od;
	    e.PE_win = win;
	}
	Put(&sendQueue, e.PE_msg, e.PE_od, e.PE_win);
    }
    ghal_crit_leave();
}

/* WinFlushSendQueue: to the change object, outside the tree lock; a
 * call from a handler of these messages leaves it to the outer one */
static void FlushSendQueue(void)
{
    PtrEvent e;

    ghal_crit_enter();
    if (wp.changeFlags & WCF_FLUSHING_QUEUE) {
	ghal_crit_leave();
	return;
    }
    wp.changeFlags |= WCF_FLUSHING_QUEUE;
    while (Get(&sendQueue, &e)) {
	WinPtrEventParams p;
	optr dest = wp.outputOD;
	ghal_crit_leave();
	p.WPEP_inputObj = e.PE_od;
	p.WPEP_win = e.PE_win;
	CObjMessageP(0, dest, e.PE_msg, &p, sizeof(p), NULL, 0);
	ghal_crit_enter();
    }
    wp.changeFlags &= (byte) ~WCF_FLUSHING_QUEUE;
    ghal_crit_leave();
}

/* ---------------------------------------------------------------------
 * WinMovePtr: fast, from the input manager
 * ------------------------------------------------------------------- */

/* WinSendWinChange; entered in the critical section, leaves it */
static void SendWinChange(WindowHandle tree, sword x, sword y)
{
    WinChangeParams p;
    optr od = wp.outputOD;

    if (od)
	wp.changeCount++;
    ghal_crit_leave();
    if (!od)
	return;
    p.WCP_x = x;
    p.WCP_y = y;
    p.WCP_tree = tree;
    CObjMessageP(MF_FORCE_QUEUE, od, KMSG_META_WIN_CHANGE, &p, sizeof(p),
		 NULL, 0);
}

/* WinCheckPtr; entered in the critical section, leaves it */
static void CheckPtr(WindowHandle tree, sword x, sword y)
{
    if (wp.changeCount ||
	(x >= wp.testBounds.R_left && x <= wp.testBounds.R_right &&
	 y >= wp.testBounds.R_top && y <= wp.testBounds.R_bottom)) {
	ghal_crit_leave();
	return;
    }
    SendWinChange(tree, x, y);
}

void WinMovePtr(word su, WindowHandle tree, sword x, sword y)
{
    WinUnderFlags old;
    WindowHandle oldTree;

    ghal_crit_enter();
    wp.locX = x;
    wp.locY = y;
    old = wp.suAtMove;
    wp.suAtMove = (WinUnderFlags) su;
    oldTree = wp.treeAtMove;
    wp.treeAtMove = tree;
    if (tree != oldTree || (WinUnderFlags) su != old)
	SendWinChange(tree, x, y);
    else
	CheckPtr(tree, x, y);
}

/* WinForceWinChange: at the last position reported (none before the
 * input manager reported one) */
static void ForceWinChange(void)
{
    ghal_crit_enter();
    if (wp.treeAtMove)
	SendWinChange(wp.treeAtMove, wp.locX, wp.locY);
    else
	ghal_crit_leave();
}

/* WinRedoMovePtr */
static void RedoMovePtr(void)
{
    ghal_crit_enter();
    if (wp.treeAtMove)
	CheckPtr(wp.treeAtMove, wp.locX, wp.locY);
    else
	ghal_crit_leave();
}

Boolean WinGrabChange(WindowHandle win, optr newObj)
{
    Boolean ok = FALSE;

    (void) win;
    ghal_crit_enter();
    if (!wp.outputOD) {
	wp.outputOD = newObj;
	ok = TRUE;
    }
    ghal_crit_leave();
    return ok;
}

void WinReleaseChange(WindowHandle win, optr obj)
{
    (void) win;
    ghal_crit_enter();
    if (wp.outputOD == obj)
	wp.outputOD = 0;
    ghal_crit_leave();
}

/* a button or key is coming: the UI must know the window under the
 * pointer first */
void WinEnsureChangeNotification(void)
{
    Boolean pending;

    ghal_crit_enter();
    pending = (Boolean) (wp.changeCount != 0);
    ghal_crit_leave();
    if (pending)
	ForceWinChange();
}

/* a visible region changes (WinPerformVis, WinPerformObscure) or a
 * window closes */
void WinChangePtrNotification(void)
{
    Boolean pending;

    ghal_crit_enter();
    /* Doug 2/18/92: a new window may take the save under area of a
     * closed one; forget the areas, so the next move sends a change */
    wp.suAtMove = 0;
    pending = (Boolean) (wp.changeCount != 0);
    ghal_crit_leave();
    if (!pending)
	ForceWinChange();
}

/* ---------------------------------------------------------------------
 * The walk (under the tree lock)
 * ------------------------------------------------------------------- */

/* WinTestIfWinInBranch: is wh in the branch of top? */
static Boolean InBranch(WindowHandle wh, WindowHandle top)
{
    while (wh) {
	Window *w;
	WindowHandle up;
	if (wh == top)
	    return TRUE;
	w = MemLock(wh);
	up = w->W_parent;
	MemUnlock(wh);
	wh = up;
    }
    return FALSE;
}

/* LowCheckInVis: in says whether the pointer is in the visible region
 * (b its band and run there) */
static void LowCheckInVis(WindowHandle wh, Boolean in, const Rectangle *b)
{
    Window *w = MemDeref(wh);

    if (in) {
	ghal_crit_enter();
	wp.testBounds = *b;
	ghal_crit_leave();
	if (!(w->W_ptrFlags & WPF_PTR_IN_VIS)) {
	    w->W_ptrFlags |= WPF_PTR_IN_VIS;
	    SendImpliedChange();
	}
	wp.impliedWin = wh;
	wp.impliedWinProc = MemOwner(wh);
	wp.impliedWinOD = w->W_inputObj;
    } else
	w->W_ptrFlags &= (WinPtrFlags) ~WPF_PTR_IN_VIS;
}

/* the visible region holds the pointer (the window must be the one
 * located, if one was) */
static Boolean TestVis(WindowHandle wh, Rectangle *b)
{
    Window *w = MemDeref(wh);

    if (wp.locatedVisWin && wp.locatedVisWin != wh)
	return FALSE;
    return GrTestPointInReg(WinReg(wh, w->W_visReg), (word) wp.lastX,
			    (word) wp.lastY, b);
}

/* CheckInVis */
static Boolean CheckInVis(WindowHandle wh)
{
    Rectangle b;
    Boolean in = TestVis(wh, &b);

    LowCheckInVis(wh, in, &b);
    return in;
}

/* CheckInUniv */
static Boolean CheckInUniv(WindowHandle wh)
{
    Window *w = MemDeref(wh);
    Rectangle b;
    Boolean in;

    if (wp.locatedVisWin)
	in = InBranch(wp.locatedVisWin, wh);
    else
	in = GrTestPointInReg(WinReg(wh, w->W_univReg), (word) wp.lastX,
			      (word) wp.lastY, &b);
    w = MemDeref(wh);
    if (in) {
	if (!(w->W_ptrFlags & WPF_PTR_IN_UNIV)) {
	    w->W_ptrFlags |= WPF_PTR_IN_UNIV;
	    SendImpliedChange();
	    SendEnterLeave(w, KMSG_META_RAW_UNIV_ENTER);
	}
    } else if (w->W_ptrFlags & WPF_PTR_IN_UNIV) {
	w->W_ptrFlags &= (WinPtrFlags) ~WPF_PTR_IN_UNIV;
	SendEnterLeave(w, KMSG_META_RAW_UNIV_LEAVE);
    }
    return in;
}

static void CheckAcross(WindowHandle first);

/* CheckInChildren */
static void CheckInChildren(WindowHandle wh)
{
    Window *w = MemLock(wh);
    WindowHandle ch = w->W_firstChild;

    MemUnlock(wh);
    if (ch)
	CheckAcross(ch);
}

/* CheckAcross: the siblings from first; down into the one the pointer
 * is in */
static void CheckAcross(WindowHandle first)
{
    WindowHandle wh, next;

    for (wh = first; wh; wh = next) {
	MemLock(wh);
	if (CheckInUniv(wh)) {
	    Boolean vis = CheckInVis(wh);
	    MemUnlock(wh);
	    if (!vis)
		CheckInChildren(wh);
	    return;
	}
	next = ((Window *) MemDeref(wh))->W_nextSibling;
	MemUnlock(wh);
    }
}

/* CheckHere: up from wh until a universe holds the pointer, then down */
static void CheckHere(WindowHandle wh)
{
    while (wh) {
	WindowHandle up;
	MemLock(wh);
	if (CheckInUniv(wh)) {
	    Boolean vis = CheckInVis(wh);
	    MemUnlock(wh);
	    if (!vis)
		CheckInChildren(wh);
	    return;
	}
	up = ((Window *) MemDeref(wh))->W_parent;
	MemUnlock(wh);
	wh = up;
    }
}

/* WinTraverseHere: from window wh (0: the tree) of tree to x, y
 * (EOREGREC, EOREGREC: off the tree) */
static void TraverseHere(WindowHandle wh, WindowHandle tree, sword x,
			 sword y)
{
    wp.lastTree = tree;
    if (!wh)
	wh = tree;
    wp.impliedWin = 0;
    wp.impliedWinProc = 0;
    wp.impliedWinOD = 0;
    wp.locatedVisWin = 0;
    if (tree && !(x == (sword) EOREGREC && y == (sword) EOREGREC)) {
	/* in a save under area, the regions do not tell: the window
	 * there is found by its bounds (WinLocateCommon) */
	Window *t = MemLock(tree);
	Rectangle pt;
	pt.R_left = pt.R_right = x;
	pt.R_top = pt.R_bottom = y;
	if (WinCheckUnder(t, &pt)) {
	    MemUnlock(tree);
	    wp.locatedVisWin = WinLocatePointC(tree, x, y);
	} else
	    MemUnlock(tree);
    }
    wp.lastX = x;
    wp.lastY = y;
    if (tree && wh) {
	Rectangle b;
	MemLock(wh);
	if (!TestVis(wh, &b))
	    LowCheckInVis(wh, FALSE, &b);	/* VIS_LEAVEs first */
	MemUnlock(wh);
	CheckHere(wh);
    }
    if (!wp.impliedWin)
	wp.lastTree = 0;
    wp.locatedVisWin = 0;
}

/* WinCommonTraverse: from where the pointer was to x, y on tree */
static void CommonTraverse(WindowHandle tree, sword x, sword y)
{
    if (wp.lastTree != tree) {
	TraverseHere(wp.impliedWin, wp.lastTree, (sword) EOREGREC,
		     (sword) EOREGREC);
	TraverseHere(tree, tree, x, y);
    } else
	TraverseHere(wp.impliedWin, tree, x, y);
}

/* ---------------------------------------------------------------------
 * Pointer images
 * ------------------------------------------------------------------- */

/* GetFinalWinAndGeode: whose images show.  Without an active geode the
 * implied window and its geode; with one, the implied window only if
 * it is the active geode's.  Then the geode's active window, if it has
 * one. */
static void FinalWinAndGeode(WindowHandle *win, GeodeHandle *geode)
{
    WindowHandle wh = wp.impliedWin;
    GeodeHandle g = wp.activeGeode;
    GeodeWinVars v;

    if (g) {
	if (wh && MemOwner(wh) != g)
	    wh = 0;
    } else
	g = wh ? MemOwner(wh) : 0;
    if (g) {
	WinReadGeodeVars(g, &v);
	if (v.GWV_activeWin)
	    wh = v.GWV_activeWin;
    }
    *win = wh;
    *geode = g;
}

/* ActivateWinPtrImages */
static void ActivateWinPtrImages(WindowHandle wh)
{
    optr gadget = 0, window = 0;

    if (wh) {
	Window *w = MemLock(wh);
	if (w) {
	    gadget = w->W_gadgetPtrImage;
	    window = w->W_windowPtrImage;
	    MemUnlock(wh);
	}
    }
    ImSetPtrImage(PIL_GADGET, gadget);
    ImSetPtrImage(PIL_WINDOW, window);
}

/* ActivateGeodePtrImage */
static void ActivateGeodePtrImage(GeodeHandle g)
{
    GeodeWinVars v;

    v.GWV_ptrImage = 0;
    if (g)
	WinReadGeodeVars(g, &v);
    ImSetPtrImage(PIL_GEODE, v.GWV_ptrImage);
}

/* UpdatePtrImages (with ptrImageSem) */
void WinUpdatePtrImages(void)
{
    WindowHandle win;
    GeodeHandle g;

    FinalWinAndGeode(&win, &g);
    if (win != wp.finalWin) {
	wp.finalWin = win;
	ActivateWinPtrImages(win);
    }
    if (g != wp.finalGeode) {
	wp.finalGeode = g;
	ActivateGeodePtrImage(g);
    }
}

WindowHandle WinPtrFinalWin(void)
{
    return wp.finalWin;
}

GeodeHandle WinPtrFinalGeode(void)
{
    return wp.finalGeode;
}

/* WinSysSetActiveGeode: the geode the mouse is down in, if any */
void WinSysSetActiveGeode(GeodeHandle gh)
{
    WinTreeP();
    WinPtrImageP();
    if (gh != wp.activeGeode) {
	wp.activeGeode = gh;
	WinUpdatePtrImages();
    }
    WinPtrImageV();
    WinTreeV();
}

void WinSetPtrImage(WindowHandle win, PtrImageLevel ptrLevel, optr ptrCh)
{
    Boolean changed = TRUE;

    WinPtrImageP();
    if (OptrToHandle(ptrCh) || OptrToChunk(ptrCh) != PIV_UPDATE) {
	Window *w = MemLock(win);
	optr *at;
	if (!w || w->W_header.LMBH_lmemType != LMEM_TYPE_WINDOW) {
	    if (w)
		MemUnlock(win);
	    WinPtrImageV();
	    return;
	}
	at = ptrLevel == PIL_GADGET ? &w->W_gadgetPtrImage :
	    &w->W_windowPtrImage;
	changed = (Boolean) (*at != ptrCh);
	*at = ptrCh;
	MemUnlock(win);
    }
    if (changed && win == wp.finalWin)
	ImSetPtrImage(ptrLevel, ptrCh);
    WinPtrImageV();
}

/* ---------------------------------------------------------------------
 * WinChangeAck and the end of a walk
 * ------------------------------------------------------------------- */

/* WinCommonEnd: the images, then the messages after the tree lock goes;
 * returns the window and its input object */
static WindowHandle CommonEnd(optr *od)
{
    WindowHandle win;

    if (!wp.impliedWin) {
	ghal_crit_enter();
	wp.testBounds.R_left = wp.testBounds.R_right = (sword) EOREGREC;
	ghal_crit_leave();
    }
    SendImpliedChange();
    WinPtrImageP();
    WinUpdatePtrImages();
    WinPtrImageV();
    *od = wp.impliedWinOD;			/* GetImpliedOD */
    win = wp.impliedWin;
    ProcessEventQueue(*od, win);
    WinTreeV();
    FlushSendQueue();
    return win;
}

WindowHandle WinChangeAck(WindowHandle win, sword x, sword y, optr *winOD)
{
    WindowHandle in;
    optr od;

    WinTreeP();
    wp.changeFlags &= (byte) ~WCF_IMPLIED_CHANGE_SENT;
    CommonTraverse(win, x, y);
    in = CommonEnd(&od);
    ghal_crit_enter();
    if (wp.changeCount)
	wp.changeCount--;
    ghal_crit_leave();
    RedoMovePtr();		/* it may have moved on meanwhile */
    if (winOD)
	*winOD = od;
    return in;
}

/* ---------------------------------------------------------------------
 * Closing and freeing (C only, see above)
 * ------------------------------------------------------------------- */

/* the leaves of a branch, children first */
static void LeaveBranch(WindowHandle wh)
{
    Window *w = MemLock(wh);
    WindowHandle ch, next;

    for (ch = w->W_firstChild; ch; ch = next) {
	Window *c = MemLock(ch);
	next = c->W_nextSibling;
	MemUnlock(ch);
	LeaveBranch(ch);
    }
    w = MemDeref(wh);
    w->W_ptrFlags &= (WinPtrFlags) ~WPF_PTR_IN_VIS;
    if (w->W_ptrFlags & WPF_PTR_IN_UNIV) {
	w->W_ptrFlags &= (WinPtrFlags) ~WPF_PTR_IN_UNIV;
	SendEnterLeave(w, KMSG_META_RAW_UNIV_LEAVE);
    }
    MemUnlock(wh);
}

/* WinClose, with the window still in the tree (the tree lock held) */
void WinPtrClosing(WindowHandle wh, WindowHandle parent)
{
    LeaveBranch(wh);
    if (wp.impliedWin && InBranch(wp.impliedWin, wh)) {
	wp.impliedWin = parent;
	wp.impliedWinProc = 0;
	wp.impliedWinOD = 0;
    }
    if (wp.lastTree == wh)
	wp.lastTree = 0;
    WinChangePtrNotification();
}

/* the window is freed (WinDecRefCount; the asm's
 * WinDeathPtrNotification had nothing to do without
 * WIN_ENTER_LEAVE_CONTROL) */
void WinPtrFreed(WindowHandle wh)
{
    GeodeHandle owner = MemOwner(wh);
    GeodeWinVars v;

    if (wp.impliedWin == wh) {
	wp.impliedWin = 0;
	wp.impliedWinProc = 0;
	wp.impliedWinOD = 0;
    }
    if (wp.lastTree == wh)
	wp.lastTree = 0;
    WinPtrImageP();
    if (wp.finalWin == wh)
	wp.finalWin = 0;
    if (owner) {
	WinReadGeodeVars(owner, &v);
	if (v.GWV_activeWin == wh) {
	    v.GWV_activeWin = 0;
	    WinWriteGeodeVars(owner, &v);
	}
    }
    WinPtrImageV();
    ghal_crit_enter();
    if (wp.treeAtMove == wh)
	wp.treeAtMove = 0;
    ghal_crit_leave();
}
