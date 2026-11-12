/*
 * imMonitors.c -- the monitor chain of the input manager (after
 * imMonitors.asm: ProcessUserInput, ImAddMonitor, ImRemoveMonitor,
 * UnlinkMonitor, CombineInputMonitor, ProcessButton, ProcessWheel,
 * ProcessKbd, SetDragEventIfNecessary, CheckForLostDrag,
 * PtrPerturbMonitor, OutputMonitor, OutputNonPtr).
 *
 * Every input event goes through the chain on the input manager's
 * thread, monitor by monitor in the order of their levels
 * (CInclude/Internal/im.h):
 *   ML_DRIVER   drivers: the keyboard driver turns MSG_IM_KBD_SCAN into
 *               MSG_META_KBD_CHAR;
 *   ML_COMBINE  pointer changes are summed into a position, buttons get
 *               their state, double presses and drags; MSG_META_MOUSE_*;
 *   ML_PTR_PERTURB  the pointer kept in a rectangle (ImConstrainMouse);
 *   ML_OUTPUT   the events go to the output (ImGrabInput).
 * A monitor returns MF_DATA to pass the event on, MF_MORE_TO_DO to be
 * asked again (message 0) once the rest of the chain is through.
 *
 * DOS16 builds without QUIET_PTR_SUPPORT and INPUT_MESSAGE_RECEIPT; so
 * does this.  A move of the pointer moves its image (imPtr.c) and tells
 * the window system (WinMovePtr); a button, key or wheel event first
 * makes sure the UI knows the window under the pointer
 * (WinEnsureChangeNotification).  Not yet: the screen saver (it
 * unblanked here), the move/resize xor, pen and ink.  The
 * repeated keys are not thinned out when handles run short.
 */
#include "kernelC.h"
#include "imInt.h"

#include <win.h>

#include <input.h>
#include <system.h>
#include <timer.h>

ImVars im;

static KSem semMonChain = { 1, 0 };
static Monitor headMonitor;		/* M_monRoutine NULL: the start */

static const byte buttonConvTab[IM_MAX_BUTTONS] = {
    BI_B0_DOWN, BI_B1_DOWN, BI_B2_DOWN, BI_B3_DOWN
};

void ImLockChain(void)
{
    KSemP(&semMonChain, KSEM_FOREVER);
}

void ImUnlockChain(void)
{
    KSemV(&semMonChain);
}

/* UnlinkMonitor; never the head */
static void UnlinkMonitor(Monitor *m)
{
    Monitor *prev = m->M_prevMonitor, *next = m->M_nextMonitor;

    prev->M_nextMonitor = next;
    if (next)
	next->M_prevMonitor = prev;
    m->M_prevMonitor = m->M_nextMonitor = NULL;
}

/* a monitor removed by the chain: ImRemoveMonitor may be waiting */
static void Released(Monitor *m)
{
    if (m->M_semMonitor)
	ThreadVSem(m->M_semMonitor);
}

/* from m back to the start: the first monitor with more to do */
static Monitor *FindMore(Monitor *m, ImEvent *ev)
{
    for (; m && m != &headMonitor; m = m->M_prevMonitor)
	if (m->M_flags & MF_MORE_TO_DO) {
	    ev->IE_message = 0;		/* "give me your data" */
	    return m;
	}
    return NULL;
}

/* ProcessUserInput: an event through the chain */
void ImProcessUserInput(ImEvent *ev)
{
    Monitor *cur, *prev;
    byte f;

    ImLockChain();
    cur = headMonitor.M_nextMonitor;
    while (cur) {
	cur->M_flags |= MF_IN_MONITOR;
	ImUnlockChain();
	f = cur->M_monRoutine(cur, ev);
	ImLockChain();
	f = (byte) ((f & (MF_REMOVE_WHEN_EMPTY | MF_REMOVE_IMMEDIATE |
			  MF_MORE_TO_DO | MF_DATA)) |
		    (cur->M_flags & ~(MF_MORE_TO_DO | MF_DATA |
				      MF_IN_MONITOR)));
	cur->M_flags = f;
	if ((f & MF_REMOVE_IMMEDIATE) ||
	    ((f & MF_REMOVE_WHEN_EMPTY) && !(f & MF_MORE_TO_DO))) {
	    prev = cur->M_prevMonitor;
	    UnlinkMonitor(cur);
	    Released(cur);
	    cur = (f & MF_DATA) ? prev->M_nextMonitor : FindMore(prev, ev);
	    if (!cur && (f & MF_DATA))
		cur = FindMore(prev, ev);
	    continue;
	}
	if ((f & MF_DATA) && cur->M_nextMonitor) {
	    cur = cur->M_nextMonitor;
	    continue;
	}
	/* used up (or nobody after it): back to one with more */
	cur = (f & MF_DATA) ? FindMore(cur, ev)
			    : FindMore(cur->M_prevMonitor, ev);
    }
    ImUnlockChain();
}

ThreadHandle ImAddMonitor(Monitor *mon, byte level,
			  ImMonitorRoutine *routine)
{
    Monitor *at;

    ImLockChain();
    mon->M_monRoutine = routine;
    mon->M_priority = (sbyte) level;
    mon->M_flags = 0;
    mon->M_semMonitor = 0;
    /* after the last one of a level not above ours */
    at = &headMonitor;
    while (at->M_nextMonitor &&
	   (byte) at->M_nextMonitor->M_priority <= level)
	at = at->M_nextMonitor;
    mon->M_prevMonitor = at;
    mon->M_nextMonitor = at->M_nextMonitor;
    if (at->M_nextMonitor)
	at->M_nextMonitor->M_prevMonitor = mon;
    at->M_nextMonitor = mon;
    ImUnlockChain();
    return im.imThread;
}

void ImRemoveMonitor(Monitor *mon, byte flags)
{
    SemaphoreHandle sem;

    ImLockChain();
    if (!mon->M_prevMonitor) {		/* not in the chain */
	ImUnlockChain();
	return;
    }
    if (!(mon->M_flags & (MF_IN_MONITOR | MF_MORE_TO_DO))) {
	UnlinkMonitor(mon);
	ImUnlockChain();
	return;
    }
    /* busy: the chain removes it (Released), we wait */
    mon->M_flags |= flags & (MF_REMOVE_WHEN_EMPTY | MF_REMOVE_IMMEDIATE);
    sem = mon->M_semMonitor = ThreadAllocSem(0);
    ImUnlockChain();
    ThreadPSem(sem);
    ThreadFreeSem(sem);
    mon->M_semMonitor = 0;
}

/* --------------------------------------------------------------------
 * ML_COMBINE
 */

/* the drag timer of a press (ImForcePtrMethod from a routine timer) */
static void DragTimer(word data, dword tickCount)
{
    (void) data; (void) tickCount;
    ImForcePtrMethod();
}

static sword Abs(sword v)
{
    return (sword) (v < 0 ? -v : v);
}

/*
 * SetDragEventIfNecessary: TRUE if button b, pressed and armed for a
 * drag, has moved dragDistance or been down dragTime (time: the PI_time
 * bits of the event); ev becomes MSG_META_MOUSE_DRAG.
 */
static Boolean SetDragEventIfNecessary(byte b, word time, ImEvent *ev)
{
    byte bit = buttonConvTab[b];
    ImButtonPress *p = &im.bps[b];
    word then, now;

    if (!(im.dragState & bit))
	return FALSE;
    if ((word) Abs((sword) (im.displayX - p->BP_pressX)) > im.dragDistance ||
	(word) Abs((sword) (im.displayY - p->BP_pressY)) > im.dragDistance)
	goto send;
    now = (word) (time & PI_time);
    then = (word) (p->BP_pressTime & PI_time);
    if (now < then)
	now = (word) (now + 16383);
    if ((word) (now - then) >= im.dragTime)
	goto send;
    return FALSE;
send:
    im.dragState &= (byte) ~bit;
    ev->IE_message = IM_META_MOUSE_DRAG;
    ev->IE_bp = (word) ((im.shiftState << 8) | b | im.buttonState);
    return TRUE;
}

/* ProcessButton; returns the MonitorFlags */
static byte ProcessButton(ImEvent *ev)
{
    word bp = ev->IE_bp;
    byte b, bit;
    ImButtonPress *p;
    dword time = ((dword) ev->IE_dx << 16) | ev->IE_cx;
    byte flags = MF_DATA;

    if (im.leftHanded && !(bp & 1))
	bp ^= 2;			/* button 0 and 2 change places */
    b = (byte) (bp & BI_BUTTON);
    bit = buttonConvTab[b];
    if (!(bp & BI_PRESS)) {
	/* CheckForLostDrag: a drag the queue held up comes first */
	if (SetDragEventIfNecessary(b, ev->IE_cx, ev)) {
	    im.delayedRelease = TRUE;
	    im.delayedEvent.IE_message = MSG_IM_BUTTON_CHANGE;
	    im.delayedEvent.IE_cx = (word) time;
	    im.delayedEvent.IE_dx = (word) (time >> 16);
	    im.delayedEvent.IE_bp = bp;
	    im.delayedEvent.IE_si = ev->IE_si;
	    ev->IE_cx = (word) im.displayX;
	    ev->IE_dx = (word) im.displayY;
	    return MF_MORE_TO_DO | MF_DATA;
	}
	im.buttonState &= (byte) ~bit;
	im.dragState &= (byte) ~bit;
    } else {
	im.buttonState |= bit;
	im.dragState |= bit;
	p = &im.bps[b];
	TimerStart(TIMER_ROUTINE_ONE_SHOT, (optr) (uintptr_t) DragTimer,
		   im.dragTime, 0, 0, NULL);
	if (time - p->BP_pressTime <= im.doubleClickTime &&
	    (word) Abs((sword) (p->BP_pressX - im.displayX)) <=
		im.doubleClickDistance &&
	    (word) Abs((sword) (p->BP_pressY - im.displayY)) <=
		im.doubleClickDistance)
	    bp |= BI_DOUBLE_PRESS;
	p->BP_pressTime = time;
	p->BP_pressX = im.displayX;
	p->BP_pressY = im.displayY;
    }
    ev->IE_message = IM_META_MOUSE_BUTTON;
    ev->IE_bp = (word) ((im.shiftState << 8) |
			((bp | im.buttonState) & 0xff));
    ev->IE_cx = (word) im.displayX;
    ev->IE_dx = (word) im.displayY;
    return flags;
}

/* ProcessKbd: <Ctrl><Alt><Del> ends GEOS; state keys update the shift
 * state from the keyboard driver */
static void ProcessKbd(ImEvent *ev)
{
    DriverRegs r;

    if (!(ev->IE_dx & CF_RELEASE) &&
	ev->IE_cx == ((CS_CONTROL << 8) | VC_SYSTEMRESET)) {
	SysShutdownType t;
	if (im.rebootOnReset)
	    t = SST_REBOOT;
	else if (im.alreadyReset)
	    t = SST_DIRTY;
	else {
	    im.alreadyReset = TRUE;
	    t = SST_CLEAN_FORCED;
	}
	if (t == SST_DIRTY)
	    SysShutdown(t, (const char *) NULL);
	else
	    SysShutdown(t);
    }
    if ((ev->IE_dx & CF_STATE_KEY) && im.kbdDriver) {
	memset(&r, 0, sizeof(r));
	DriverCall(im.kbdDriver, DR_KBD_GET_KBD_STATE, &r);
	im.shiftState = (byte) r.DR_ax;
    }
}

/* the pointer: a change summed into the position */
static byte ProcessPtr(ImEvent *ev)
{
    sword x = (sword) ev->IE_cx, y = (sword) ev->IE_dx;
    word bp = ev->IE_bp;
    byte b;

    im.changeX = im.changeY = 0;
    if (!(bp & PI_absX)) {
	im.changeX = x;
	x = (sword) (x + im.pointerX);
    }
    if (!(bp & PI_absY)) {
	im.changeY = y;
	y = (sword) (y + im.pointerY);
    }
    im.pointerX = im.displayX = x;
    im.pointerY = im.displayY = y;
    ev->IE_cx = (word) x;
    ev->IE_dx = (word) y;
    if (im.dragState & (BI_B0_DOWN | BI_B1_DOWN | BI_B2_DOWN | BI_B3_DOWN))
	for (b = 0; b < IM_MAX_BUTTONS; b++)
	    if (SetDragEventIfNecessary(b, bp, ev))
		return MF_DATA;
    ev->IE_message = IM_META_MOUSE_PTR;
    ev->IE_bp = (word) ((im.shiftState << 8) | im.buttonState);
    return MF_DATA;
}

/* CombineInputMonitor */
static byte CombineInputMonitor(Monitor *mon, ImEvent *ev)
{
    (void) mon;
    /* a real user (the screen saver, MaybeUnblankScreens): anything but
     * a pointer event that does not move */
    if (ev->IE_message == MSG_IM_PTR_CHANGE) {
	sword dx = (sword) ev->IE_cx, dy = (sword) ev->IE_dx;
	if (ev->IE_bp & PI_absX)
	    dx = (sword) (dx - im.pointerX);
	if (ev->IE_bp & PI_absY)
	    dy = (sword) (dy - im.pointerY);
	if (dx || dy)
	    (void) ImMaybeUnblankScreens();
    } else if (ev->IE_message)
	(void) ImMaybeUnblankScreens();
    switch (ev->IE_message) {
    case MSG_IM_PTR_CHANGE:
	return ProcessPtr(ev);
    case MSG_IM_BUTTON_CHANGE:
	return ProcessButton(ev);
    case MSG_IM_MOUSE_WHEEL_VERTICAL:
	/* ProcessWheel: dh the wheel, < 0 up, > 0 down */
	ev->IE_message = IM_META_MOUSE_WHEEL_VERTICAL;
	ev->IE_bp = (word) ((im.shiftState << 8) | (ev->IE_dx >> 8));
	ev->IE_cx = (word) im.displayX;
	ev->IE_dx = (word) im.displayY;
	return MF_DATA;
    case IM_META_KBD_CHAR:
	ProcessKbd(ev);
	return MF_DATA;
    case 0:
	/* asked again: the release a lost drag held back */
	if (im.delayedRelease) {
	    im.delayedRelease = FALSE;
	    *ev = im.delayedEvent;
	    return ProcessButton(ev);
	}
	return MF_DATA;
    default:
	return MF_DATA;
    }
}

/* --------------------------------------------------------------------
 * ML_PTR_PERTURB: PtrPerturbMonitor, while ImConstrainMouse is on
 */
byte ImPtrPerturbMonitor(Monitor *mon, ImEvent *ev)
{
    sword x = (sword) ev->IE_cx, y = (sword) ev->IE_dx;
    word leave = 0;

    (void) mon;
    if (ev->IE_message != IM_META_MOUSE_PTR || !im.constraining)
	return MF_DATA;
    if (x < im.constrainXMin) {
	x = im.constrainXMin;
	leave |= PTR_LEAVE_LEFT;
    }
    if (x > im.constrainXMax) {
	x = im.constrainXMax;
	leave |= PTR_LEAVE_RIGHT;
    }
    im.pointerX = x;
    if (y < im.constrainYMin) {
	y = im.constrainYMin;
	leave |= PTR_LEAVE_TOP;
    }
    if (y > im.constrainYMax) {
	y = im.constrainYMax;
	leave |= PTR_LEAVE_BOTTOM;
    }
    im.pointerY = y;
    ev->IE_cx = (word) x;
    ev->IE_dx = (word) y;
    if (leave && im.constrainOD) {
	IMParams p;
	p.IMP_cx = leave;
	p.IMP_dx = p.IMP_bp = p.IMP_si = 0;
	CObjMessageP(MF_FORCE_QUEUE, im.constrainOD, IM_META_LEAVE_CONSTRAIN,
		     &p, sizeof(word), NULL, 0);
    }
    return MF_DATA;
}

/* --------------------------------------------------------------------
 * ML_OUTPUT
 */

/* the message to the output: cx, dx, bp (goc's three words) */
static void Send(const ImEvent *ev, word flags)
{
    struct { word cx, dx, bp; } p;
    optr od = im.outputOD;

    if (!od)
	return;
    p.cx = ev->IE_cx;
    p.dx = ev->IE_dx;
    p.bp = ev->IE_bp;
    CObjMessageP(flags, od, ev->IE_message, &p, sizeof(p), NULL, 0);
}

/* OutputMonitor */
static byte OutputMonitor(Monitor *mon, ImEvent *ev)
{
    sword x, y;
    word flags = MF_FORCE_QUEUE;

    (void) mon;
    if (ev->IE_message == IM_META_MOUSE_PTR) {
	x = (sword) ev->IE_cx;
	y = (sword) ev->IE_dx;
	if (im.haveScreen) {
	    if (x < im.screenXMin) {
		im.changeX = (sword) (im.changeX - (x - im.screenXMin));
		x = im.pointerX = im.screenXMin;
	    } else if (x > im.screenXMax) {
		im.changeX = (sword) (im.changeX - (x - im.screenXMax));
		x = im.pointerX = im.screenXMax;
	    }
	    if (y < im.screenYMin) {
		im.changeY = (sword) (im.changeY - (y - im.screenYMin));
		y = im.pointerY = im.screenYMin;
	    } else if (y > im.screenYMax) {
		im.changeY = (sword) (im.changeY - (y - im.screenYMax));
		y = im.pointerY = im.screenYMax;
	    }
	}
	if (im.inMoveResize) {
	    /* OutputUpdateScreenXor: the end, or the xor's limits */
	    ImUpdateScreenXor(ev->IE_bp, &x, &y);
	    im.pointerX = x;
	    im.pointerY = y;
	}
	ev->IE_cx = (word) x;
	ev->IE_dx = (word) y;
	if (!im.drawnValid || x != im.drawnX || y != im.drawnY) {
	    /* the image on the screen, then the window under it */
	    word su;
	    im.drawnX = x;
	    im.drawnY = y;
	    im.drawnValid = TRUE;
	    su = ImCallMovePtr(x, y);
	    if (im.pointerWin)
		WinMovePtr(su, im.pointerWin, x, y);
	}
	/*
	 * DOS16 folded the event into the last one in the output's queue
	 * if that was a pointer event with the same button info
	 * (CombineMouseEvent, MF_CUSTOM).  The C kernel has no custom
	 * combining: a pointer event with the button info of the last one
	 * sent replaces the last event of the queue if that is one too.
	 */
	if (im.ptrSent && im.lastSentBp == ev->IE_bp)
	    flags |= MF_CHECK_DUPLICATE | MF_REPLACE | MF_CHECK_LAST_ONLY |
		     MF_CAN_DISCARD_IF_DESPERATE;
	im.ptrSent = TRUE;
	im.lastSentBp = ev->IE_bp;
	Send(ev, flags);
	return 0;
    }
    /* OutputNonPtr */
    if (ev->IE_message == IM_META_MOUSE_BUTTON ||
	ev->IE_message == IM_META_KBD_CHAR ||
	ev->IE_message == IM_META_MOUSE_WHEEL_VERTICAL)
	WinEnsureChangeNotification();	/* the UI knows the window first */
    if (ev->IE_message == IM_META_MOUSE_BUTTON && im.constraining &&
	(buttonConvTab[ev->IE_bp & BI_BUTTON] & im.constrainButtons))
	im.constraining = FALSE;	/* constrained while it is down */
    if (ev->IE_message == IM_META_MOUSE_BUTTON && im.inMoveResize)
	ImXorButton(ev->IE_bp);
    Send(ev, MF_FORCE_QUEUE);
    return 0;
}

static Monitor combineMonitor, outputMonitor;

/* InitIMMonitors */
void ImInitMonitors(void)
{
    ImAddMonitor(&combineMonitor, ML_COMBINE, CombineInputMonitor);
    ImAddMonitor(&outputMonitor, ML_OUTPUT, OutputMonitor);
}
