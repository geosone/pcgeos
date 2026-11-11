/*
 * geodesEvent.c -- events, event queues and ObjMessage of the C kernel
 * (after geodesEvent.asm).
 *
 * An event is a SIG_EVENT_REG handle (SIG_EVENT_STACK when its
 * parameters did not fit into the event) with a KEvent: destination,
 * message, a copy of the parameter structure (Port32/ABI.md section 5)
 * and, for a call to another thread, where the reply goes and the
 * semaphore the caller waits on.  A queue is a SIG_QUEUE handle with a
 * KQueue: events linked through HE_next, a semaphore counting them and
 * the thread attached to it.
 *
 * CObjMessageP routes like ObjMessage: the destination's handle names
 * the thread that runs it (thread, queue with a thread, process = its
 * first thread, object block = its burden thread).  Messages for the
 * running thread are direct calls unless MF_FORCE_QUEUE; others become
 * events, and a call blocks until the receiver dispatched it.
 *
 * Interrupt code (event timers) may send with MF_FORCE_QUEUE: events then
 * come from a reserve that normal code keeps filled, and handles only
 * from the free list (no memory allocation under a hardware interrupt).
 */
#include "kernelC.h"
#include "ghal.h"

#include <string.h>

#define EVENT_INLINE	32		/* parameter bytes kept in the event */
#define EVENT_RESERVE	32		/* events kept for interrupt code */

typedef struct KEvent {
    struct KEvent *E_free;		/* reserve list */
    optr	E_dest;
    Message	E_msg;
    word	E_flags;		/* MessageFlags it was sent with */
    dword	E_size;			/* of the parameters */
    void	*E_params;		/* E_inline or allocated */
    void	*E_reply;		/* call: the caller's reply buffer */
    dword	E_replySize;
    KSem	*E_done;		/* call: V when dispatched */
    union {
	byte	b[EVENT_INLINE];
	dword	align[EVENT_INLINE / 4];
	void	*p;
    } E_inline;
} KEvent;

typedef struct {
    Handle	Q_head, Q_tail;		/* events, via HE_next */
    word	Q_count;
    KSem	Q_sem;			/* counts the events */
    ThreadHandle Q_thread;		/* attached thread, 0 */
} KQueue;

static KEvent *reserve;
static word reserveCount;
dword kernelEventsDropped;

/* ---------------------------------------------------------------------
 * Events
 * ------------------------------------------------------------------- */

static void Refill(void)
{
    while (reserveCount < EVENT_RESERVE) {
	KEvent *e = ghal_mem_alloc(sizeof(KEvent), GHAL_MEM_ZERO);
	if (!e)
	    return;
	ghal_crit_enter();
	e->E_free = reserve;
	reserve = e;
	reserveCount++;
	ghal_crit_leave();
    }
}

static KEvent *EventOf(EventHandle ev)
{
    HandleEntry *he = HandleEntryOf(ev);

    if (he->HE_type != SIG_EVENT_REG && he->HE_type != SIG_EVENT_STACK)
	KernelFatal(NOT_AN_EVENT_HANDLE);
    return he->HE_addr;
}

static KQueue *QueueOf(QueueHandle qh)
{
    HandleEntry *he = HandleEntryOf(qh);

    if (he->HE_type != SIG_QUEUE)
	KernelFatal(NOT_A_QUEUE_HANDLE);
    return he->HE_addr;
}

static GeodeHandle OwnerNow(void)
{
    if (kernelCurrentThread && HandleEntryOf(kernelCurrentThread)->HE_owner)
	return HandleEntryOf(kernelCurrentThread)->HE_owner;
    return kernelCurrentGeode;
}

/* a new event; 0 if there is no memory (or, in interrupt code, no
 * reserve) */
static EventHandle EventNew(optr dest, Message msg, const void *params,
			    dword size, word flags)
{
    int irq = ghal_irq_active();
    KEvent *e;
    Handle h;
    void *data = NULL;

    if (!irq)
	Refill();
    if (size > EVENT_INLINE) {
	if (irq)
	    return 0;
	data = ghal_mem_alloc(size, 0);
	if (!data)
	    return 0;
    }
    ghal_crit_enter();
    e = reserve;
    if (e) {
	reserve = e->E_free;
	reserveCount--;
    }
    ghal_crit_leave();
    if (!e && !irq)
	e = ghal_mem_alloc(sizeof(KEvent), 0);
    h = e ? HandleAlloc(data ? SIG_EVENT_STACK : SIG_EVENT_REG, OwnerNow())
	  : 0;
    if (!h) {
	if (e) {
	    ghal_crit_enter();
	    e->E_free = reserve;
	    reserve = e;
	    reserveCount++;
	    ghal_crit_leave();
	}
	ghal_mem_free(data);
	return 0;
    }
    memset(e, 0, sizeof(*e));
    e->E_dest = dest;
    e->E_msg = msg;
    e->E_flags = flags;
    e->E_size = size;
    e->E_params = data ? data : e->E_inline.b;
    if (size)
	memcpy(e->E_params, params, size);
    HandleEntryOf(h)->HE_addr = e;
    return h;
}

void ObjFreeMessage(EventHandle event)
{
    KEvent *e = EventOf(event);

    if (e->E_params != e->E_inline.b)
	ghal_mem_free(e->E_params);
    HandleFree(event);
    ghal_crit_enter();
    if (reserveCount < EVENT_RESERVE) {
	e->E_free = reserve;
	reserve = e;
	reserveCount++;
	e = NULL;
    }
    ghal_crit_leave();
    if (e)
	ghal_mem_free(e);
}

Message ObjGetMessageInfo(EventHandle event, optr *dest)
{
    KEvent *e = EventOf(event);

    if (dest)
	*dest = e->E_dest;
    return e->E_msg;
}

dword MessageSetDestination(EventHandle event, optr dest)
{
    EventOf(event)->E_dest = dest;
    return 0;
}

void ObjSetEventInfo(EventHandle event, Message msg, optr dest)
{
    KEvent *e = EventOf(event);

    e->E_msg = msg;
    e->E_dest = dest;
}

EventHandle ObjDuplicateMessage(EventHandle msg)
{
    KEvent *e = EventOf(msg);
    EventHandle dup = EventNew(e->E_dest, e->E_msg, e->E_params, e->E_size,
			       e->E_flags);

    if (!dup)
	KernelFatal(OUT_OF_HANDLES);
    return dup;
}

/*
 * CompareStatusEvents of lmemGCNList.asm: is `newEv' the status
 * `oldEv' already gave?  Same message and parameters (not the
 * destination: a status event has been sent on to objects since).  With
 * different data blocks, the blocks must hold the same bytes and the
 * parameters may differ only where they hold the blocks' handles.
 */
Boolean EventSameStatus(EventHandle newEv, MemHandle newData,
			EventHandle oldEv, MemHandle oldData)
{
    KEvent *a, *b;
    byte *copy;
    dword i, size;
    Boolean same;

    if (!newEv || !oldEv)
	return FALSE;
    a = EventOf(newEv);
    b = EventOf(oldEv);
    if (a->E_msg != b->E_msg || a->E_size != b->E_size)
	return FALSE;
    if (newData == oldData)
	return memcmp(a->E_params, b->E_params, a->E_size) == 0;
    if (!newData || !oldData)
	return FALSE;
    size = MemGetInfo(newData, MGIT_SIZE);
    if (size != MemGetInfo(oldData, MGIT_SIZE))
	return FALSE;
    same = memcmp(MemLock(newData), MemLock(oldData), size) == 0;
    MemUnlock(oldData);
    MemUnlock(newData);
    if (!same)
	return FALSE;
    copy = ghal_mem_alloc(b->E_size ? b->E_size : 1, 0);
    if (!copy)
	return FALSE;
    memcpy(copy, b->E_params, b->E_size);
    for (i = 0; i + sizeof(Handle) <= b->E_size; i += sizeof(Handle)) {
	Handle h;
	memcpy(&h, copy + i, sizeof(h));
	if (h == oldData) {
	    h = newData;
	    memcpy(copy + i, &h, sizeof(h));
	}
    }
    same = memcmp(copy, a->E_params, a->E_size) == 0;
    ghal_mem_free(copy);
    return same;
}

/* parameter structure of an event (Port32: no MessageDataStruct
 * registers) */
const void *KernelEventParams(EventHandle event, dword *size)
{
    KEvent *e = EventOf(event);

    if (size)
	*size = e->E_size;
    return e->E_params;
}

/* ---------------------------------------------------------------------
 * Queues
 * ------------------------------------------------------------------- */

QueueHandle GeodeAllocQueue(void)
{
    KQueue *q = ghal_mem_alloc(sizeof(KQueue), GHAL_MEM_ZERO);
    Handle h;

    if (!q)
	return 0;
    h = HandleAlloc(SIG_QUEUE, OwnerNow());
    if (!h) {
	ghal_mem_free(q);
	return 0;
    }
    KSemInit(&q->Q_sem, 0);
    HandleEntryOf(h)->HE_addr = q;
    return h;
}

/* inside ghal_crit */
static EventHandle Pop(KQueue *q)
{
    EventHandle ev = q->Q_head;

    if (ev) {
	q->Q_head = HandleEntryOf(ev)->HE_next;
	if (!q->Q_head)
	    q->Q_tail = 0;
	q->Q_count--;
    }
    return ev;
}

/* inside ghal_crit */
static void Link(KQueue *q, EventHandle ev, int atFront)
{
    HandleEntry *he = HandleEntryOf(ev);

    if (atFront) {
	he->HE_next = q->Q_head;
	q->Q_head = ev;
	if (!q->Q_tail)
	    q->Q_tail = ev;
    } else {
	he->HE_next = 0;
	if (q->Q_tail)
	    HandleEntryOf(q->Q_tail)->HE_next = ev;
	else
	    q->Q_head = ev;
	q->Q_tail = ev;
    }
    q->Q_count++;
}

void GeodeFreeQueue(QueueHandle qh)
{
    KQueue *q = QueueOf(qh);
    EventHandle ev;

    for (;;) {
	ghal_crit_enter();
	ev = Pop(q);
	ghal_crit_leave();
	if (!ev)
	    break;
	ObjFreeMessage(ev);
    }
    if (q->Q_thread)
	ThreadOf(q->Q_thread)->T_eventQueue = 0;
    HandleFree(qh);
    ghal_mem_free(q);
}

void KQueueAttach(QueueHandle qh, ThreadHandle th)
{
    QueueOf(qh)->Q_thread = th;
}

void KQueueDetach(QueueHandle qh)
{
    QueueOf(qh)->Q_thread = 0;
}

word GeodeInfoQueue(QueueHandle qh)
{
    return QueueOf(qh)->Q_count;
}

void QueuePostMessage(QueueHandle qh, EventHandle event, word flags)
{
    KQueue *q = QueueOf(qh);

    EventOf(event);
    ghal_crit_enter();
    Link(q, event, (flags & MF_INSERT_AT_FRONT) != 0);
    KSemV(&q->Q_sem);
    ghal_crit_leave();
}

EventHandle QueueGetMessage(QueueHandle qh)
{
    KQueue *q = QueueOf(qh);
    EventHandle ev;

    do {
	KSemP(&q->Q_sem, KSEM_FOREVER);
	ghal_crit_enter();
	ev = Pop(q);
	ghal_crit_leave();
    } while (!ev);			/* flushed away meanwhile */
    Refill();
    return ev;
}

/* Move the events of `source' for `obj' (all if obj is 0) to the end, or
 * with MF_INSERT_AT_FRONT the front, of `dest' (geodesEvent.asm). */
void GeodeFlushQueue(QueueHandle source, QueueHandle dest, optr obj,
		     word messageFlags)
{
    KQueue *s = QueueOf(source), *d = QueueOf(dest);
    Handle moved = 0, movedTail = 0, keep = 0, keepTail = 0, ev;
    word n = 0;

    ghal_crit_enter();
    while ((ev = Pop(s)) != 0) {
	HandleEntry *he = HandleEntryOf(ev);
	int take = !obj || ((KEvent *) he->HE_addr)->E_dest == obj;
	Handle *head = take ? &moved : &keep, *tail = take ? &movedTail
							  : &keepTail;
	he->HE_next = 0;
	if (*tail)
	    HandleEntryOf(*tail)->HE_next = ev;
	else
	    *head = ev;
	*tail = ev;
    }
    /* what stays: back into source, its semaphore keeps counting it */
    for (ev = keep; ev; ) {
	Handle next = HandleEntryOf(ev)->HE_next;
	Link(s, ev, 0);
	ev = next;
    }
    if (messageFlags & MF_INSERT_AT_FRONT) {
	/* keep their order: insert them back to front */
	Handle list[256];
	word i, c = 0;
	for (ev = moved; ev; ev = HandleEntryOf(ev)->HE_next) {
	    if (c == 256)
		KernelFatal(QUEUE_FLUSH_TOO_LARGE);
	    list[c++] = ev;
	}
	for (i = c; i-- > 0; )
	    Link(d, list[i], 1);
	n = c;
    } else
	for (ev = moved; ev; ) {
	    Handle next = HandleEntryOf(ev)->HE_next;
	    Link(d, ev, 0);
	    ev = next;
	    n++;
	}
    /* the semaphores: source lost n events (a getter that already did
     * its P finds the queue empty and waits again), dest gained them */
    if (s->Q_sem.S_value > 0)
	s->Q_sem.S_value = s->Q_sem.S_value > (sword) n
			   ? (sword) (s->Q_sem.S_value - n) : 0;
    while (n--)
	KSemV(&d->Q_sem);
    ghal_crit_leave();
}

/* ---------------------------------------------------------------------
 * Routing
 * ------------------------------------------------------------------- */

/* the thread that runs `dest' (0 if none) and the queue that reaches it */
static void Route(optr dest, ThreadHandle *thread, QueueHandle *queue)
{
    Handle h = OptrToHandle(dest);
    HandleEntry *he = HandleEntryOf(h);
    ThreadHandle th = 0;
    QueueHandle q = 0;

    switch (he->HE_type) {
    case SIG_THREAD:
	th = h;
	break;
    case SIG_QUEUE:
	q = h;
	th = ((KQueue *) he->HE_addr)->Q_thread;
	break;
    case HT_MEM:
	/* a process (core block, owns itself): its first thread; an
	 * object block: its burden thread */
	th = (ThreadHandle) he->HE_otherInfo;
	if (!th)
	    KernelFatal(NO_THREAD_FOR_DESTINATION);
	break;
    default:
	KernelFatal(ILLEGAL_DESTINATION);
    }
    if (th && !q)
	q = ThreadOf(th)->T_eventQueue;
    *thread = th;
    *queue = q;
}

/* duplicate checking (MF_CHECK_DUPLICATE); TRUE: the new event is not
 * needed (combined or discarded) */
static int CheckDuplicates(KQueue *q, word flags, optr dest, Message msg,
			   const void *params, dword size)
{
    Handle ev = (flags & MF_CHECK_LAST_ONLY) ? q->Q_tail : q->Q_head;

    for (; ev; ev = HandleEntryOf(ev)->HE_next) {
	KEvent *e = HandleEntryOf(ev)->HE_addr;
	if (e->E_msg == msg && e->E_dest == dest && !e->E_done) {
	    if ((flags & MF_REPLACE) && size <= EVENT_INLINE &&
		(e->E_params == e->E_inline.b || size <= e->E_size)) {
		memcpy(e->E_params, params, size);
		e->E_size = size;
	    }
	    return 1;
	}
	if (flags & MF_CHECK_LAST_ONLY)
	    break;
    }
    return (flags & MF_DISCARD_IF_NO_MATCH) != 0;
}

/* scratch for replies nobody wants (a @send of a message with a return
 * value): the thunk writes it somewhere */
typedef union {
    byte	b[256];
    dword	d;
    void	*p;
    double	f;
} ReplyScratch;

void *CObjMessageP(word flags, optr dest, Message message,
		   const void *params, dword paramSize,
		   void *reply, dword replySize)
{
    ThreadHandle th;
    QueueHandle qh;
    KQueue *q;
    EventHandle ev;

    KernelPreemptPoint();
    if (flags & MF_RECORD) {
	ev = EventNew(dest, message, params, paramSize, flags);
	if (!ev)
	    KernelFatal(OUT_OF_HANDLES);
	if (reply && replySize >= sizeof(EventHandle))
	    *(EventHandle *) reply = ev;
	return reply;
    }
    if (OptrToHandle(dest) == 0)
	return reply;			/* to nobody: nothing happens */
    if (flags & MF_CUSTOM)
	KernelFatal(MF_CUSTOM_NOT_SUPPORTED);
    if ((flags & (MF_CALL | MF_FORCE_QUEUE)) == (MF_CALL | MF_FORCE_QUEUE))
	KernelFatal(CANNOT_HAVE_BOTH_MF_FORCE_QUEUE_AND_MF_CALL);
    Route(dest, &th, &qh);

    if (!(flags & MF_FORCE_QUEUE) && th && th == kernelCurrentThread &&
	!ghal_irq_active()) {
	ReplyScratch scratch;
	KernelDeliver(dest, message, params, reply ? reply : &scratch);
	return reply;
    }
    if (!qh)
	KernelFatal(NO_QUEUE_FOR_DESTINATION);
    q = QueueOf(qh);

    if (flags & MF_CALL) {
	KSem done;
	if (ghal_irq_active())
	    KernelFatal(NOT_ALLOWED_TO_PSEM_IN_INTERRUPT_CODE);
	ev = EventNew(dest, message, params, paramSize, flags);
	if (!ev)
	    KernelFatal(OUT_OF_HANDLES);
	KSemInit(&done, 0);
	EventOf(ev)->E_reply = reply;
	EventOf(ev)->E_replySize = replySize;
	EventOf(ev)->E_done = &done;
	QueuePostMessage(qh, ev, 0);
	KSemP(&done, KSEM_FOREVER);
	return reply;
    }

    ghal_crit_enter();
    if ((flags & MF_CHECK_DUPLICATE) &&
	CheckDuplicates(q, flags, dest, message, params, paramSize)) {
	ghal_crit_leave();
	return reply;
    }
    ghal_crit_leave();
    ev = EventNew(dest, message, params, paramSize, flags);
    if (!ev) {
	if (!ghal_irq_active())
	    KernelFatal(OUT_OF_HANDLES);
	kernelEventsDropped++;		/* interrupt code: no reserve left */
	return reply;
    }
    QueuePostMessage(qh, ev, flags & MF_INSERT_AT_FRONT);
    return reply;
}

/* Deliver an event (MessageDispatch): as ObjMessage with `flags' (the
 * dispatching thread calls directly what it runs itself).  The reply
 * goes to the waiting caller if the event is a call, else to `reply'. */
/*
 * The events a thread is dispatching, innermost first (a chain of records
 * on its stack, KThread.T_dispatching).  A handler that ends its thread
 * (ThreadDestroy) does not return to free its event and wake its caller:
 * KernelDispatchAbandon does that.  The asm kernel had freed the event
 * already, its parameters being in registers.
 */
typedef struct DispatchRec {
    EventHandle		DR_event;	/* to free, or 0 */
    KSem		*DR_done;	/* the caller of an MF_CALL, or NULL */
    struct DispatchRec	*DR_prev;
} DispatchRec;

void *CMessageDispatchP(EventHandle event, word flags, void *reply,
			dword replySize)
{
    KEvent *e = EventOf(event);
    void *to = e->E_done ? e->E_reply : reply;
    dword toSize = e->E_done ? e->E_replySize : replySize;
    KThread *t = ThreadOf(KernelThisThread());
    DispatchRec rec;

    rec.DR_event = (flags & MF_DISPATCH_DONT_FREE) ? 0 : event;
    rec.DR_done = e->E_done;
    rec.DR_prev = t->T_dispatching;
    t->T_dispatching = &rec;
    e->E_done = NULL;
    CObjMessageP((word) (flags & (MF_CALL | MF_FORCE_QUEUE |
				  MF_INSERT_AT_FRONT)),
		 e->E_dest, e->E_msg, e->E_params, e->E_size, to, toSize);
    t->T_dispatching = rec.DR_prev;
    if (rec.DR_done)
	KSemV(rec.DR_done);
    if (rec.DR_event)
	ObjFreeMessage(event);
    return reply;
}

void KernelDispatchAbandon(KThread *t)
{
    DispatchRec *r;

    for (r = t->T_dispatching; r; r = r->DR_prev) {
	if (r->DR_done)
	    KSemV(r->DR_done);
	if (r->DR_event)
	    ObjFreeMessage(r->DR_event);
    }
    t->T_dispatching = NULL;
}

/* the event with another message or destination (0: keep) */
void ObjDispatchMessage(EventHandle event, Message replacementMessage,
			optr replacementDest, MessageFlags flags)
{
    KEvent *e = EventOf(event);

    if (replacementMessage)
	e->E_msg = replacementMessage;
    if (replacementDest)
	e->E_dest = replacementDest;
    CMessageDispatchP(event, flags, NULL, 0);
}

/* event timers (timerList.c): (dword tick, word id) as parameters */
static void TimerEvent(optr dest, Message msg, dword tick, word id)
{
    struct { dword tick; word id; } p;

    p.tick = tick;
    p.id = id;
    CObjMessageP(MF_FORCE_QUEUE, dest, msg, &p, sizeof(p), NULL, 0);
}

void KernelEventInit(void)
{
    Refill();
    kernelTimerEventHook = TimerEvent;
}
