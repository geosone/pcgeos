/*
 * test_kernel_win.c -- the window system of the C kernel
 * (Library/Kernel/Win: winCreDest.c, winWindows.c, winIndividual.c,
 * winState.c, winTrans.c, winUtils.c).
 *
 * A root window on a fake video driver that records DR_VID_RECT (the
 * washes) and windows on it:
 *   - opening: the regions (universe, visible, invalid), the wash with
 *     the background, MSG_META_EXPOSED to the exposure object;
 *   - updates: GrBeginUpdate/GrEndUpdate, WinAckUpdate, suspending;
 *   - the mask for a GState (WinValWinStruct): visible without
 *     invalid, within the update region;
 *   - overlapping windows, priority, moving, resizing, closing;
 *   - WinInvalReg, WinGetInfo/WinSetInfo, transformations, locating a
 *     point;
 *   - the window under the pointer: MSG_META_WIN_CHANGE, WinChangeAck,
 *     the enters, leaves and implied changes, closing under the pointer,
 *     the pointer images, ImSetPtrWin.
 */
#include "kernelC.h"
#include "Win/winInt.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

#define XY(x, y)	(((dword) (word) (y) << 16) | (word) (x))

/* the fake driver: the washes, the pointer */
static int movePtrs, setPtrs;
static const void *lastPtrDef;
static int rects;
static Rectangle lastRect;
static dword lastColor;

static Boolean Strategy(word function, DriverRegs *r)
{
    if (function == DR_VID_RECT) {
	GState *gs = r->DR_dsSi;
	Window *w = r->DR_esDi;
	const CommonAttr *ca = (const CommonAttr *) ((byte *) gs + r->DR_si);
	rects++;
	lastRect.R_left = (sword) r->DR_ax;
	lastRect.R_top = (sword) r->DR_bx;
	lastRect.R_right = (sword) r->DR_cx;
	lastRect.R_bottom = (sword) r->DR_dx;
	lastColor = ca->CA_colorIndex;
	(void) w;
	return FALSE;
    }
    if (function == DR_VID_CHECK_UNDER || function == DR_VID_INFO_UNDER)
	r->DR_ax = r->DR_di = 0;		/* no areas */
    if (function == DR_VID_MOVEPTR) {
	movePtrs++;
	r->DR_ax = 0;				/* no areas */
    }
    if (function == DR_VID_SETPTR) {
	setPtrs++;
	lastPtrDef = r->DR_dsSi;
    }
    return function == DR_VID_SAVE_UNDER;	/* refused */
}

static QueueHandle q;

/* the exposures queued for window wh since the last call for it */
static WindowHandle seenWin[8];
static int seenCount[8];

static int Exposed(WindowHandle wh)
{
    EventHandle ev;
    dword size;
    int i, n;

    while (GeodeInfoQueue(q)) {
	const WindowHandle *p;
	ev = QueueGetMessage(q);
	p = KernelEventParams(ev, &size);
	if (ObjGetMessageInfo(ev, NULL) == KMSG_META_EXPOSED && p)
	    for (i = 0; i < 8; i++)
		if (seenWin[i] == *p || !seenWin[i]) {
		    seenWin[i] = *p;
		    seenCount[i]++;
		    break;
		}
	ObjFreeMessage(ev);
    }
    for (i = 0; i < 8; i++)
	if (seenWin[i] == wh) {
	    n = seenCount[i];
	    seenCount[i] = 0;
	    return n;
	}
    return 0;
}

static Rectangle Bounds(WindowHandle wh, int which)
{
    Window *w = MemLock(wh);
    ChunkHandle ch = which == 0 ? w->W_visReg : which == 1 ? w->W_invalReg :
		     which == 2 ? w->W_univReg : w->W_maskReg;
    Rectangle r;

    GrGetPtrRegBounds(WinReg(wh, ch), &r);
    MemUnlock(wh);
    return r;
}

static int In(WindowHandle wh, int which, sword x, sword y)
{
    Window *w = MemLock(wh);
    ChunkHandle ch = which == 0 ? w->W_visReg : which == 1 ? w->W_invalReg :
		     which == 2 ? w->W_univReg : w->W_maskReg;
    Rectangle r;
    int in = GrTestPointInReg(WinReg(wh, ch), (word) x, (word) y, &r) != 0;

    MemUnlock(wh);
    return in;
}

static int Null(WindowHandle wh, int which)
{
    return Bounds(wh, which).R_left == (sword) EOREGREC;
}

#define RECT(r, l, t, ri, b) \
    ((r).R_left == (l) && (r).R_top == (t) && (r).R_right == (ri) && \
     (r).R_bottom == (b))

/* the next message to the change object: its message and parameters */
static QueueHandle cq;

static Message NextPtr(void *params, dword want)
{
    EventHandle ev;
    const void *p;
    dword size;
    Message m;

    if (!GeodeInfoQueue(cq))
	return 0;
    ev = QueueGetMessage(cq);
    p = KernelEventParams(ev, &size);
    m = ObjGetMessageInfo(ev, NULL);
    memset(params, 0, want);
    if (p)
	memcpy(params, p, size < want ? size : want);
    ObjFreeMessage(ev);
    return m;
}

/* the next is an enter, leave or implied change for window wh */
static int Next(Message msg, WindowHandle wh, optr od)
{
    WinPtrEventParams p;
    Message m = NextPtr(&p, sizeof(p));

    if (m != msg || p.WPEP_win != wh || p.WPEP_inputObj != od) {
	printf("  got %u win %lx, want %u win %lx\n", (unsigned) m,
	       (unsigned long) p.WPEP_win, (unsigned) msg,
	       (unsigned long) wh);
	return 0;
    }
    return 1;
}

/* the next is MSG_META_WIN_CHANGE; its parameters */
static int Change(WinChangeParams *c)
{
    return NextPtr(c, sizeof(*c)) == KMSG_META_WIN_CHANGE;
}

/* a move, answered as the flow object would */
static WindowHandle MoveAndAck(WindowHandle root, sword x, sword y,
			       optr *od)
{
    WinChangeParams c;

    WinMovePtr(0, root, x, y);
    if (!Change(&c) || c.WCP_x != x || c.WCP_y != y || c.WCP_tree != root)
	return (WindowHandle) -1;
    return WinChangeAck(c.WCP_tree, c.WCP_x, c.WCP_y, od);
}

/* the messages so far, the changes answered */
static void Drain(void)
{
    WinChangeParams c;
    Message m;
    optr od;

    while ((m = NextPtr(&c, sizeof(c))) != 0)
	if (m == KMSG_META_WIN_CHANGE)
	    (void) WinChangeAck(c.WCP_tree, c.WCP_x, c.WCP_y, &od);
}

/* the window under the pointer (winNotification.c) */
static void PtrTest(WindowHandle root, optr od)
{
    WindowHandle c, d, in;
    WinChangeParams wc;
    optr iod, chg, img;
    MemHandle ib;
    ChunkHandle ich;
    Rectangle r;

    cq = GeodeAllocQueue();
    chg = ConstructOptr(cq, 0);
    CHECK(WinGrabChange(0, chg) && !WinGrabChange(0, od));
    c = WinOpen(root, od, od, CMT_CLOSEST, C_WHITE, 0, 0, 0, 0, 0, NULL,
		200, 200, 299, 299);
    d = WinOpen(c, od, od, CMT_CLOSEST, C_RED, 0, 0, 0, 0, 0, NULL,
		20, 20, 39, 39);
    WinGetWinScreenBounds(d, &r);
    CHECK(RECT(r, 220, 220, 239, 239));
    Drain();				/* the opens' changes */

    /* onto C: the implied change first, then the enters, outside in */
    in = MoveAndAck(root, 250, 250, &iod);
    CHECK(in == c && iod == od);
    CHECK(Next(KMSG_META_IMPLIED_WIN_CHANGE, c, od));
    CHECK(Next(KMSG_META_RAW_UNIV_ENTER, root, 0));
    CHECK(Next(KMSG_META_RAW_UNIV_ENTER, c, od));
    CHECK(!NextPtr(&wc, sizeof(wc)));
    /* within the band of C's visible region: nothing */
    WinMovePtr(0, root, 260, 290);
    CHECK(!NextPtr(&wc, sizeof(wc)));
    /* into D, C's child */
    CHECK(MoveAndAck(root, 225, 225, &iod) == d);
    CHECK(Next(KMSG_META_IMPLIED_WIN_CHANGE, d, od));
    CHECK(Next(KMSG_META_RAW_UNIV_ENTER, d, od));
    CHECK(!NextPtr(&wc, sizeof(wc)));
    /* out onto the root: the leaves inside out, then the change */
    CHECK(MoveAndAck(root, 400, 400, &iod) == root);
    CHECK(Next(KMSG_META_RAW_UNIV_LEAVE, d, od));
    CHECK(Next(KMSG_META_RAW_UNIV_LEAVE, c, od));
    CHECK(Next(KMSG_META_IMPLIED_WIN_CHANGE, root, 0));
    CHECK(!NextPtr(&wc, sizeof(wc)));

    /* a key while a change is outstanding: another one */
    WinEnsureChangeNotification();
    CHECK(!NextPtr(&wc, sizeof(wc)));	/* none outstanding */
    WinMovePtr(0, root, 250, 250);
    WinEnsureChangeNotification();
    CHECK(Change(&wc) && Change(&wc) && !NextPtr(&wc, sizeof(wc)));
    CHECK(WinChangeAck(root, 250, 250, &iod) == c);
    CHECK(WinChangeAck(root, 250, 250, &iod) == c);
    CHECK(Next(KMSG_META_IMPLIED_WIN_CHANGE, c, od));
    CHECK(Next(KMSG_META_RAW_UNIV_ENTER, c, od));
    /* each answer names the window, even where nothing changed */
    CHECK(Next(KMSG_META_IMPLIED_WIN_CHANGE, c, od));
    CHECK(!NextPtr(&wc, sizeof(wc)));
    /* into another save under area at the same place */
    WinMovePtr(1, root, 250, 250);
    CHECK(Change(&wc) && WinChangeAck(root, 250, 250, &iod) == c);
    CHECK(Next(KMSG_META_IMPLIED_WIN_CHANGE, c, od));
    CHECK(!NextPtr(&wc, sizeof(wc)));

    /* the input manager: the root window takes the pointer and draws
     * it through its driver */
    movePtrs = 0;
    ImSetPtrWin(root, 100, 100);
    CHECK(movePtrs == 1);

    /* the pointer images follow the window: PIL_WINDOW of C */
    ib = MemAllocLMem(LMEM_TYPE_GENERAL, 0);
    MemLock(ib);
    ich = LMemAlloc(ib, 40);
    MemUnlock(ib);
    img = ConstructOptr(ib, ich);
    setPtrs = 0;
    WinSetPtrImage(c, PIL_WINDOW, img);
    CHECK(setPtrs == 1 && lastPtrDef != NULL);
    WinSetPtrImage(c, PIL_WINDOW, img);	/* no change */
    CHECK(setPtrs == 1);
    WinSetPtrImage(c, PIL_GADGET, 0);
    CHECK(setPtrs == 1);
    CHECK(MoveAndAck(root, 225, 225, &iod) == d);	/* D: none */
    CHECK(setPtrs == 2 && lastPtrDef == NULL);
    Drain();

    /* closing D under the pointer: its leave, then C */
    Drain();
    WinClose(d);
    CHECK(Change(&wc) && WinChangeAck(root, 225, 225, &iod) == c);
    CHECK(Next(KMSG_META_RAW_UNIV_LEAVE, d, od));
    CHECK(Next(KMSG_META_IMPLIED_WIN_CHANGE, c, od));
    CHECK(!NextPtr(&wc, sizeof(wc)));
    WinClose(c);
    CHECK(Change(&wc) && WinChangeAck(root, 225, 225, &iod) == root);
    CHECK(Next(KMSG_META_RAW_UNIV_LEAVE, c, od));
    CHECK(Next(KMSG_META_IMPLIED_WIN_CHANGE, root, 0));
    CHECK(!NextPtr(&wc, sizeof(wc)));

    WinReleaseChange(0, od);		/* not the holder */
    CHECK(!WinGrabChange(0, od));
    WinReleaseChange(0, chg);
    CHECK(WinGrabChange(0, od));
    WinReleaseChange(0, od);
    MemFree(ib);
    GeodeFreeQueue(cq);
    while (Exposed(0))
	;
}

int main(void)
{
    WindowHandle root, a, b;
    GStateHandle g;
    GState *gs;
    Window *w;
    optr od;
    word v;
    byte col[4];
    Handle h;
    Rectangle r;
    PointDWord pd;
    WinPrivateData pdata = { 1, 2, 3, 4 }, pget;

    kernelCurrentGeode = 0x2340;
    q = GeodeAllocQueue();
    od = ConstructOptr(q, 0);

    /* the root: plain, no driver yet (nothing drawn), then ours */
    root = WinOpen(0, 0, 0, WCF_PLAIN | CMT_CLOSEST, C_BLUE, 0, 0,
		   WPF_ROOT, 0, 0, NULL, 0, 0, 639, 479);
    CHECK(root != 0);
    WinSetInfo(root, WIT_STRATEGY, (optr) (uintptr_t) (void *) Strategy);
    CHECK(RECT(Bounds(root, 0), 0, 0, 639, 479));
    CHECK(Null(root, 1));		/* plain: nothing stays invalid */
    WinInvalReg(root, NULL, 0, 0, 639, 479);
    CHECK(rects == 1 && RECT(lastRect, 0, 0, 639, 479) &&
	  lastColor == C_BLUE && Null(root, 1));

    /* A: 10,10 - 109,109, white, exposed */
    rects = 0;
    a = WinOpen(root, od, od, CMT_CLOSEST, C_WHITE, 0, 0, 0, 0, 0, NULL,
		10, 10, 109, 109);
    CHECK(RECT(Bounds(a, 0), 10, 10, 109, 109) &&
	  RECT(Bounds(a, 1), 10, 10, 109, 109));
    CHECK(rects == 1 && RECT(lastRect, 10, 10, 109, 109) &&
	  lastColor == C_WHITE);
    CHECK(Exposed(a) == 1);
    CHECK(!In(root, 0, 50, 50) && In(root, 0, 5, 5) && In(root, 0, 110, 50));
    w = MemLock(a);
    CHECK((w->W_regFlags & WRF_EXPOSE_PENDING) && w->W_parent == root &&
	  w->W_priority == ((LAYER_PRIO_STD << 4) | WIN_PRIO_STD));
    MemUnlock(a);

    /* the update */
    g = GrCreateState(a);
    GrBeginUpdate(g);
    CHECK(Null(a, 1));			/* invalid became update */
    gs = MemLock(g);
    WinTreeP();
    MemLock(a);
    WinValWinStruct(g, gs, a);
    w = MemDeref(a);
    CHECK((w->W_grFlags & WGF_MASK_VALID) && (w->W_grFlags & WGF_MASK_SIMPLE)
	  && RECT(w->W_maskRect, 10, 10, 109, 109) && w->W_curState == g);
    MemUnlock(a);
    WinTreeV();
    MemUnlock(g);
    GrEndUpdate(g);
    CHECK(Exposed(a) == 0);		/* nothing new */

    /* B in front, overlapping: A shrinks (no exposure), B is exposed */
    rects = 0;
    b = WinOpen(root, od, od, CMT_CLOSEST, C_RED, 0, 0, 0, 0, 0, NULL,
		50, 50, 149, 149);
    CHECK(Exposed(b) == 1 && Exposed(a) == 0 && rects == 1);
    CHECK(!In(a, 0, 60, 60) && In(a, 0, 20, 20) && In(b, 0, 60, 60));
    w = MemLock(root);
    CHECK(w->W_firstChild == b && w->W_lastChild == a);
    MemUnlock(root);
    CHECK(WinLocatePointC(root, 60, 60) == b &&
	  WinLocatePointC(root, 20, 20) == a &&
	  WinLocatePointC(root, 300, 300) == root);

    /* a drawing mask: the visible region without the invalid one */
    WinInvalReg(a, NULL, 0, 0, 9, 9);	/* window coordinates: 10..19 */
    CHECK(RECT(Bounds(a, 1), 10, 10, 19, 19) && Exposed(a) == 1);
    gs = MemLock(g);
    WinTreeP();
    MemLock(a);
    WinValWinStruct(g, gs, a);
    w = MemDeref(a);
    CHECK(!(w->W_grFlags & WGF_MASK_SIMPLE));
    MemUnlock(a);
    WinTreeV();
    MemUnlock(g);
    CHECK(!In(a, 3, 15, 15) && In(a, 3, 25, 25) && !In(a, 3, 60, 60));
    WinAckUpdate(a);
    CHECK(Null(a, 1));

    /* suspended: no exposure until resumed */
    WinSuspendUpdate(a);
    WinInvalReg(a, NULL, 0, 0, 9, 9);
    CHECK(Exposed(a) == 0);
    WinUnSuspendUpdate(a);
    CHECK(Exposed(a) == 1);
    WinAckUpdate(a);

    /* A to the front: the part B covered is exposed */
    WinChangePriority(a, 0, 0);
    w = MemLock(root);
    CHECK(w->W_firstChild == a);
    MemUnlock(root);
    CHECK(In(a, 0, 60, 60) && !In(b, 0, 60, 60) && Exposed(a) == 1 &&
	  RECT(Bounds(a, 1), 50, 50, 109, 109));
    WinAckUpdate(a);
    /* behind again */
    WinChangePriority(a, WPF_PLACE_BEHIND, 0);
    w = MemLock(root);
    CHECK(w->W_firstChild == b && w->W_lastChild == a);
    MemUnlock(root);

    /* moving B away: all of it is exposed again, A gets back its part */
    Exposed(b);
    WinAckUpdate(b);			/* one exposure at a time */
    WinMove(b, 300, 0, 0);
    CHECK(RECT(Bounds(b, 0), 350, 50, 449, 149) && Exposed(b) == 1 &&
	  Exposed(a) == 1 && In(a, 0, 60, 60));
    WinAckUpdate(a);
    WinAckUpdate(b);
    WinMove(b, 200, 200, WPF_ABS);
    CHECK(RECT(Bounds(b, 0), 200, 200, 299, 299));
    WinAckUpdate(b);

    /* resizing A */
    WinResize(a, NULL, 0, 0, 49, 29, 0);
    CHECK(RECT(Bounds(a, 0), 10, 10, 59, 39) && In(root, 0, 100, 100) &&
	  Exposed(a) == 1);
    WinAckUpdate(a);

    /* a child of A: cut out of A's visible region, moved with A */
    {
	WindowHandle c = WinOpen(a, od, od, CMT_CLOSEST, C_YELLOW, 0, 0, 0,
				 0, 0, NULL, 5, 5, 14, 14);
	CHECK(RECT(Bounds(c, 0), 15, 15, 24, 24) && !In(a, 0, 20, 20) &&
	      In(a, 0, 30, 30) && Exposed(c) == 1);
	CHECK(WinLocatePointC(root, 20, 20) == c);
	WinAckUpdate(c);
	WinMove(a, 100, 0, 0);
	CHECK(RECT(Bounds(c, 0), 115, 15, 124, 24) &&
	      RECT(Bounds(a, 2), 110, 10, 159, 39) && Exposed(c) == 1 &&
	      Exposed(a) == 1 && In(root, 0, 20, 20));
	WinAckUpdate(a);
	WinAckUpdate(c);
	WinMove(a, -100, 0, 0);
	Exposed(a);
	WinAckUpdate(a);
	WinClose(c);
	CHECK(In(a, 0, 20, 20) && Exposed(a) == 1);
	WinAckUpdate(a);
    }

    /* the transformation: translated by -5, -5 and scaled by 2 */
    WinApplyTranslation(a, (WWFixedAsDWord) -(5L << 16),
			(WWFixedAsDWord) -(5L << 16), WIF_DONT_INVALIDATE);
    CHECK(WinTransform(a, 5, 5) == XY(10, 10) &&
	  WinUntransform(a, 10, 10) == XY(5, 5));
    WinApplyScale(a, 2L << 16, 2L << 16, WIF_INVALIDATE);
    CHECK(WinTransform(a, 5, 5) == XY(15, 15) && Exposed(a) == 1);
    WinTransformDWord(a, 7, 7, &pd);
    CHECK(pd.PD_x == 19 && pd.PD_y == 19);
    WinUntransformDWord(a, 19, 19, &pd);
    CHECK(pd.PD_x == 7 && pd.PD_y == 7);
    CHECK(GrTransform(g, 5, 5) == XY(15, 15));	/* through the GState */
    WinSetNullTransform(a, WIF_DONT_INVALIDATE);
    CHECK(WinTransform(a, 5, 5) == XY(15, 15) && GrTransform(g, 0, 0) ==
	  XY(10, 10));
    WinAckUpdate(a);

    /* information */
    WinSetInfo(a, WIT_PRIVATE_DATA, (optr) (uintptr_t) &pdata);
    WinGetInfo(a, WIT_PRIVATE_DATA, &pget);
    CHECK(pget.WPD_ax == 1 && pget.WPD_dx == 4);
    WinSetInfo(a, WIT_COLOR, C_GREEN | (CMT_DITHER << 8));
    WinGetInfo(a, WIT_COLOR, col);
    CHECK(col[0] == C_GREEN && col[1] == CMT_DITHER);
    WinGetInfo(a, WIT_PARENT_WIN, &h);
    CHECK(h == root);
    WinGetInfo(a, WIT_PREV_SIBLING_WIN, &h);
    CHECK(h == b);
    WinGetInfo(g, WIT_FLAGS, &v);	/* through the GState */
    CHECK(!(v & WRF_EXPOSE_PENDING));
    WinGetInfo(a, WIT_EXPOSURE_OBJ, &od);
    CHECK(od == ConstructOptr(q, 0));
    WinGetWinScreenBounds(g, &r);
    CHECK(RECT(r, 10, 10, 59, 39));

    /* closing B: the root gets its area back (plain: no exposure) */
    WinClose(b);
    CHECK(In(root, 0, 250, 250) && Null(root, 1));
    w = MemLock(root);
    CHECK(w->W_firstChild == a && w->W_lastChild == a);
    MemUnlock(root);
    GrDestroyState(g);
    WinClose(a);
    CHECK(In(root, 0, 20, 20));
    w = MemLock(root);
    CHECK(w->W_firstChild == 0);
    MemUnlock(root);
    PtrTest(root, ConstructOptr(q, 0));
    WinClose(root);

    printf("%s (%d failed)\n", fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
