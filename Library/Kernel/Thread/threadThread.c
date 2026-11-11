/*
 * threadThread.c -- threads and the scheduler of the C kernel (after
 * threadThread.asm and the queue routines of threadSem.asm).
 *
 * The scheduler stays GEOS's own (Port32/GHAL-Concept.md 4.1): a run
 * queue ordered by priority (lower number first, first come first served
 * within a priority), and exactly one running thread.  A thread is a
 * SIG_THREAD handle whose KThread holds a GHAL context (ghal_ctx); the
 * scheduler picks the next thread and switches to its context.  When no
 * thread can run, the GHAL idles until an interrupt makes one ready.
 *
 * All scheduler state is changed inside ghal_crit.  Threads switch when
 * they block, end, yield, or when a thread of better priority becomes
 * ready outside interrupt code.  Not yet: time slices and CPU usage
 * aging (they come with the timer interrupt), the acknowledgement of
 * ThreadDestroy (needs messages), event queues, thread exceptions.
 */
#include "kernelC.h"
#include "ghal.h"

ThreadHandle kernelCurrentThread;

volatile int kernelReschedulePending;

static Handle runQueue;			/* ready threads, via T_nextQ */
static byte   sliceLeft = TIME_SLICE;	/* ticks of the running thread */
static Handle deadList;			/* ended, to be released */
static dword  timedWaiters;		/* blocked threads with a deadline */
static uint64_t tickBase;

#define NS_PER_TICK	(1000000000u / 60u)

dword KernelTicks(void)
{
    uint64_t now = ghal_time_monotonic_ns();

    if (tickBase == 0)
	tickBase = now ? now : 1;
    return (dword) ((now - tickBase) / NS_PER_TICK);
}

KThread *ThreadOf(ThreadHandle th)
{
    HandleEntry *he = HandleEntryOf(th);

    if (he->HE_type != SIG_THREAD)
	KernelFatal(ILLEGAL_THREAD);
    return he->HE_addr;
}

/* ---------------------------------------------------------------------
 * Queues of threads, best priority first, FIFO within a priority.
 * ------------------------------------------------------------------- */
static void QueueInsert(Handle *q, Handle th)
{
    KThread *t = ThreadOf(th);

    while (*q && ThreadOf(*q)->T_curPriority <= t->T_curPriority)
	q = &ThreadOf(*q)->T_nextQ;
    t->T_nextQ = *q;
    *q = th;
}

static void QueueRemove(Handle *q, Handle th)
{
    while (*q && *q != th)
	q = &ThreadOf(*q)->T_nextQ;
    if (*q) {
	*q = ThreadOf(th)->T_nextQ;
	ThreadOf(th)->T_nextQ = 0;
    }
}

static Handle QueuePop(Handle *q)
{
    Handle th = *q;

    if (th) {
	*q = ThreadOf(th)->T_nextQ;
	ThreadOf(th)->T_nextQ = 0;
    }
    return th;
}

static void MakeReady(Handle th)
{
    KThread *t = ThreadOf(th);

    t->T_state = TS_READY;
    t->T_waitSem = NULL;
    if (t->T_deadline) {
	t->T_deadline = 0;
	timedWaiters--;
    }
    QueueInsert(&runQueue, th);
}

/* ---------------------------------------------------------------------
 * Dispatching
 * ------------------------------------------------------------------- */

/* wake the blocked threads whose ThreadPTimedSem timed out */
static int WakeTimedOut(Handle th, HandleEntry *he, void *ctx)
{
    KThread *t = he->HE_addr;
    dword now = *(dword *) ctx;

    if (he->HE_type == SIG_THREAD && t->T_state == TS_BLOCKED &&
	t->T_deadline && (sdword) (now - t->T_deadline) >= 0) {
	KSem *s = t->T_waitSem;
	QueueRemove(&s->S_queue, th);
	s->S_value++;			/* it does not take the semaphore */
	t->T_timedOut = 1;
	MakeReady(th);
    }
    return timedWaiters == 0;
}

static void CheckTimeouts(void)
{
    if (timedWaiters) {
	dword now = KernelTicks();
	HandleEnum(WakeTimedOut, &now);
    }
}

/* release ended threads (not the running one) */
static void Reap(void)
{
    while (deadList) {
	Handle th = QueuePop(&deadList);
	KThread *t = ThreadOf(th);
	ghal_ctx_destroy(t->T_ctx);
	ghal_mem_free(t);
	HandleFree(th);
    }
}

/* Run the best ready thread; the running thread has been queued, blocked
 * or ended already.  Inside ghal_crit.  Returns when the caller runs
 * again (never for an ended thread). */
static void Dispatch(void)
{
    Handle prev = kernelCurrentThread, next;
    KThread *pt = ThreadOf(prev), *nt;

    for (;;) {
	CheckTimeouts();
	if (runQueue)
	    break;
	ghal_idle();			/* nothing can run: wait for an IRQ */
    }
    next = QueuePop(&runQueue);
    nt = ThreadOf(next);
    nt->T_state = TS_RUNNING;
    kernelCurrentThread = next;
    sliceLeft = TIME_SLICE;
    if (next == prev)
	return;
    if (pt->T_state == TS_DEAD)
	ghal_ctx_exit(nt->T_ctx);
    ghal_ctx_switch(nt->T_ctx);
    Reap();
}

/* switch if a ready thread is better than the running one (not from
 * interrupt code: there the switch waits for the next dispatch) */
static void MaybePreempt(void)
{
    Handle cur = kernelCurrentThread;

    if (cur && runQueue &&
	ThreadOf(runQueue)->T_curPriority < ThreadOf(cur)->T_curPriority) {
	if (ghal_irq_active()) {
	    kernelReschedulePending = 1;
	    return;
	}
	MakeReady(cur);
	Dispatch();
    }
}

int ThreadBlockOn(KSem *s, dword timeout)
{
    Handle cur = KernelThisThread();
    KThread *t = ThreadOf(cur);

    if (ghal_irq_active())
	KernelFatal(NOT_ALLOWED_TO_PSEM_IN_INTERRUPT_CODE);
    t->T_state = TS_BLOCKED;
    t->T_waitSem = s;
    t->T_timedOut = 0;
    if (timeout != KSEM_FOREVER) {
	t->T_deadline = KernelTicks() + timeout;
	if (t->T_deadline == 0)
	    t->T_deadline = 1;
	timedWaiters++;
    }
    QueueInsert(&s->S_queue, cur);
    Dispatch();
    return t->T_timedOut;
}

void ThreadWakeOne(KSem *s)
{
    Handle th = QueuePop(&s->S_queue);

    if (th) {
	MakeReady(th);
	MaybePreempt();
    }
}

void KernelPreemptPoint(void)
{
    if (kernelReschedulePending && !ghal_irq_active()) {
	kernelReschedulePending = 0;
	KernelYield();
    }
}

/* the running thread used a tick (timerInt.asm, step 9) */
void ThreadTick(void)
{
    Handle cur = kernelCurrentThread;
    KThread *t;

    CheckTimeouts();
    if (!cur)
	return;
    t = ThreadOf(cur);
    if (t->T_cpuUsage < 255)
	t->T_cpuUsage++;
    if (t->T_curPriority < PRIORITY_IDLE - 1)
	t->T_curPriority++;
    if (--sliceLeft == 0) {
	sliceLeft = TIME_SLICE;
	if (runQueue)
	    kernelReschedulePending = 1;
    } else if (runQueue &&
	       ThreadOf(runQueue)->T_curPriority < t->T_curPriority)
	kernelReschedulePending = 1;	/* a woken thread is better */
}

/* every second: halve the CPU usage (timerInt.asm, step 7) */
static int Decay(Handle th, HandleEntry *he, void *ctx)
{
    KThread *t = he->HE_addr;
    word p;

    (void) th;
    (void) ctx;
    if (he->HE_type != SIG_THREAD)
	return 0;
    t->T_cpuUsage >>= 1;
    p = (word) (t->T_basePriority + t->T_cpuUsage);
    if (p < PRIORITY_IDLE)
	t->T_curPriority = (byte) p;
    return 0;
}

void ThreadDecay(void)
{
    Handle q;

    HandleEnum(Decay, NULL);
    /* priorities changed: order the run queue again */
    q = runQueue;
    runQueue = 0;
    while (q) {
	Handle th = QueuePop(&q);
	QueueInsert(&runQueue, th);
    }
}

void KernelYield(void)
{
    ghal_crit_enter();
    CheckTimeouts();
    if (kernelCurrentThread && runQueue &&
	ThreadOf(runQueue)->T_curPriority <=
	ThreadOf(kernelCurrentThread)->T_curPriority) {
	MakeReady(kernelCurrentThread);
	Dispatch();
    }
    ghal_crit_leave();
}

/* ---------------------------------------------------------------------
 * Creating and ending threads
 * ------------------------------------------------------------------- */

static KThread *NewThread(ThreadHandle *out, GeodeHandle owner,
			  ThreadPriority priority)
{
    Handle th = HandleAlloc(SIG_THREAD, owner);
    KThread *t;

    *out = th;
    if (!th)
	return NULL;
    t = ghal_mem_alloc(sizeof(KThread), GHAL_MEM_ZERO);
    if (!t) {
	HandleFree(th);
	return NULL;
    }
    t->T_basePriority = t->T_curPriority = priority;
    HandleEntryOf(th)->HE_addr = t;
    return t;
}

ThreadHandle KernelThreadInit(ThreadPriority priority)
{
    ThreadHandle th;
    KThread *t;

    if (kernelCurrentThread)
	return kernelCurrentThread;
    t = NewThread(&th, kernelCurrentGeode, priority);
    if (!t)
	KernelFatal(THREAD_CREATE_FAILED);
    t->T_ctx = ghal_ctx_self();
    t->T_state = TS_RUNNING;
    kernelCurrentThread = th;
    return th;
}

ThreadHandle KernelThisThread(void)
{
    return kernelCurrentThread ? kernelCurrentThread
			       : KernelThreadInit(PRIORITY_STANDARD);
}

static void ThreadStart(void *arg)
{
    ThreadHandle th = (ThreadHandle) (uintptr_t) arg;
    KThread *t;

    ghal_crit_enter();
    Reap();
    ghal_crit_leave();
    t = ThreadOf(th);
    ThreadDestroy(t->T_start(t->T_startValue), 0, 0);
}

ThreadHandle ThreadCreate(word priority, word valueToPass,
			  word (*startRoutine)(word valuePassed),
			  word stackSize, GeodeHandle owner)
{
    ThreadHandle th;
    KThread *t;

    KernelThisThread();
    t = NewThread(&th, owner, (ThreadPriority) priority);
    if (!t)
	return 0;
    t->T_start = startRoutine;
    t->T_startValue = valueToPass;
    /* 16-bit stack sizes are too small for 32/64-bit code; the GHAL
     * gives every context at least its minimum */
    if (ghal_ctx_create(ThreadStart, (void *) (uintptr_t) th, stackSize,
			&t->T_ctx) != GHAL_OK) {
	ghal_mem_free(t);
	HandleFree(th);
	return 0;
    }
    ghal_crit_enter();
    MakeReady(th);
    MaybePreempt();
    ghal_crit_leave();
    return th;
}

void ThreadDestroy(word errorCode, optr ackObject, word ackData)
{
    Handle cur = KernelThisThread();
    KThread *t = ThreadOf(cur);

    (void) ackObject;			/* MSG_META_ACK: with messages */
    (void) ackData;
    ghal_crit_enter();
    t->T_exitCode = errorCode;
    t->T_state = TS_DEAD;
    t->T_nextQ = deadList;
    deadList = cur;
    Dispatch();				/* does not return */
    ghal_crit_leave();
}

/* ---------------------------------------------------------------------
 * Information and priorities
 * ------------------------------------------------------------------- */

dword ThreadGetInfo(ThreadHandle th, ThreadGetInfoType info)
{
    KThread *t;

    if (!th)
	th = KernelThisThread();
    t = ThreadOf(th);
    switch (info) {
    case TGIT_PRIORITY_AND_USAGE:
	return t->T_curPriority | ((dword) t->T_cpuUsage << 8);
    case TGIT_THREAD_HANDLE:
	return th;
    case TGIT_QUEUE_HANDLE:
	return t->T_eventQueue;
    default:
	break;
    }
    KernelFatal(THREAD_GET_INFO_BAD_PARAMETER);
    return 0;				/* not reached */
}

void ThreadModify(ThreadHandle th, word newBasePriority,
		  ThreadModifyFlags flags)
{
    KThread *t;

    if (!th)
	th = KernelThisThread();
    t = ThreadOf(th);
    ghal_crit_enter();
    if (flags & TMF_ZERO_USAGE)
	t->T_cpuUsage = 0;
    if (flags & TMF_BASE_PRIO)
	t->T_basePriority = (byte) newBasePriority;
    t->T_curPriority = t->T_basePriority;
    /* keep the queues ordered */
    if (t->T_state == TS_READY) {
	QueueRemove(&runQueue, th);
	QueueInsert(&runQueue, th);
    } else if (t->T_state == TS_BLOCKED) {
	QueueRemove(&t->T_waitSem->S_queue, th);
	QueueInsert(&t->T_waitSem->S_queue, th);
    }
    if (th == kernelCurrentThread)
	KernelYield();
    else
	MaybePreempt();
    ghal_crit_leave();
}
