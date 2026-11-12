/*
 * timerList.c -- timers of the C kernel (CInclude/timer.h; after
 * timerList.asm and timerMisc.asm).
 *
 * A timer is a SIG_TIMER handle with a KTimer, kept in a list sorted by
 * the tick it is due.  The timer interrupt (timerInt.c) runs the due
 * ones: a routine timer calls its routine there (interrupt context, as
 * in the asm kernel), an event timer sends its message through
 * kernelTimerEventHook (the message system).  Continual timers go back
 * into the list.  Interrupt code cannot free memory on every host
 * (DJGPP's malloc under a hardware interrupt), so ended timers wait on a
 * list that TimerReap empties from normal code.
 *
 * Ticks are 1/60 s of the GHAL clock (KernelTicks), so TimerGetCount and
 * the timeouts of ThreadPTimedSem agree.
 *
 * Real-time timers (TIMER_*_REAL_TIME) go off at a date and minute: ticks
 * is the TimerCompressedDate, interval the hours (high byte) and minutes.
 * They wait on their own list, sorted by that time; every second the
 * interrupt checks it against the clock (KernelRealTimeNow), and so does
 * TimerSetDateAndTime.  A due one sends its message or calls its routine
 * once with the date and time it was set for (TimerCompressedDate << 16
 * | hours << 8 | minutes) where the others pass the tick count, and id 0.
 */
#include "kernelC.h"
#include "ghal.h"

typedef struct KTimer {
    struct KTimer *TI_next;
    TimerHandle	TI_handle;
    TimerType	TI_type;
    dword	TI_due;			/* tick */
    word	TI_interval;		/* continual: ticks */
    word	TI_id;
    optr	TI_dest;		/* object, or the routine */
    Message	TI_msg;			/* message, or the routine's data */
} KTimer;

static KTimer *timerList;		/* sorted by TI_due */
static KTimer *realTimeList;		/* sorted by TI_due (date, time) */
static KTimer *endedList;		/* to be freed by TimerReap */
static word nextId = 1;

void (*kernelTimerEventHook)(optr dest, Message msg, dword tick, word id);
dword kernelTimerEventsDropped;

static int Due(dword due, dword now)
{
    return (sdword) (now - due) >= 0;
}

/* inside ghal_crit */
static void Insert(KTimer *t)
{
    KTimer **p = &timerList;

    while (*p && Due((*p)->TI_due, t->TI_due))	/* FIFO for equal ticks */
	p = &(*p)->TI_next;
    t->TI_next = *p;
    *p = t;
}

void TimerReap(void)
{
    KTimer *list;

    ghal_crit_enter();
    list = endedList;
    endedList = NULL;
    ghal_crit_leave();
    while (list) {
	KTimer *next = list->TI_next;
	HandleFree(list->TI_handle);
	ghal_mem_free(list);
	list = next;
    }
}

static int IsRoutine(TimerType type)
{
    return type == TIMER_ROUTINE_ONE_SHOT ||
	   type == TIMER_ROUTINE_CONTINUAL ||
	   type == TIMER_MS_ROUTINE_ONE_SHOT;
}

static int IsRealTime(TimerType type)
{
    return type == TIMER_EVENT_REAL_TIME || type == TIMER_ROUTINE_REAL_TIME;
}

static int IsContinual(TimerType type)
{
    return type == TIMER_ROUTINE_CONTINUAL || type == TIMER_EVENT_CONTINUAL;
}

TimerHandle TimerStart(TimerType timerType, optr destObject, word ticks,
		       Message msg, word interval, word *id)
{
    KTimer *t;
    Handle h;

    TimerReap();
    switch (timerType) {
    case TIMER_ROUTINE_ONE_SHOT:
    case TIMER_ROUTINE_CONTINUAL:
    case TIMER_EVENT_ONE_SHOT:
    case TIMER_EVENT_CONTINUAL:
    case TIMER_MS_ROUTINE_ONE_SHOT:
    case TIMER_EVENT_REAL_TIME:
    case TIMER_ROUTINE_REAL_TIME:
	break;
    default:
	KernelFatal(BAD_TIMER_TYPE);
    }
    if (IsRealTime(timerType)) {
	ghal_datetime now;
	KernelGetDateTime(&now);	/* the interrupt counts from here */
    } else if (timerType == TIMER_MS_ROUTINE_ONE_SHOT)	/* ms -> ticks, up */
	ticks = (word) (((dword) ticks * TIMER_INTERRUPT_RATE + 999) / 1000);
    if (ticks == 0 && !IsRealTime(timerType))
	ticks = 1;			/* at the next tick */
    t = ghal_mem_alloc(sizeof(KTimer), GHAL_MEM_ZERO);
    if (!t)
	return 0;
    h = HandleAlloc(SIG_TIMER, MemOwner(KernelThisThread()) ?
		    MemOwner(KernelThisThread()) : kernelCurrentGeode);
    if (!h) {
	ghal_mem_free(t);
	return 0;
    }
    HandleEntryOf(h)->HE_addr = t;
    t->TI_handle = h;
    t->TI_type = timerType;
    t->TI_interval = interval;
    t->TI_dest = destObject;
    t->TI_msg = msg;
    ghal_crit_enter();
    t->TI_id = nextId++;
    if (nextId == 0)
	nextId = 1;
    if (IsRealTime(timerType)) {
	KTimer **p = &realTimeList;
	t->TI_due = ((dword) ticks << 16) | interval;
	while (*p && (*p)->TI_due <= t->TI_due)
	    p = &(*p)->TI_next;
	t->TI_next = *p;
	*p = t;
    } else {
	t->TI_due = KernelTicks() + ticks;
	Insert(t);
    }
    ghal_crit_leave();
    if (id)
	*id = t->TI_id;
    return h;
}

/* TRUE if not found (ended already, or not this timer) */
Boolean TimerStop(TimerHandle th, word id)
{
    KTimer **p;
    Boolean notFound = TRUE;

    ghal_crit_enter();
    for (p = &timerList; *p; p = &(*p)->TI_next)
	if ((*p)->TI_handle == th && (*p)->TI_id == id)
	    break;
    if (!*p)
	for (p = &realTimeList; *p; p = &(*p)->TI_next)
	    if ((*p)->TI_handle == th && (*p)->TI_id == id)
		break;
    if (*p) {
	KTimer *t = *p;
	*p = t->TI_next;
	t->TI_next = endedList;
	endedList = t;
	notFound = FALSE;
    }
    ghal_crit_leave();
    TimerReap();
    return notFound;
}

/* from the timer interrupt, inside ghal_crit */
void TimerCheckList(dword now)
{
    while (timerList && Due(timerList->TI_due, now)) {
	KTimer *t = timerList;
	timerList = t->TI_next;
	if (IsRoutine(t->TI_type)) {
	    TimerRoutine *fn = (TimerRoutine *) (uintptr_t) t->TI_dest;
	    fn((word) t->TI_msg, now);
	} else if (kernelTimerEventHook)
	    kernelTimerEventHook(t->TI_dest, t->TI_msg, now,
				 IsContinual(t->TI_type) ? t->TI_interval
							 : t->TI_id);
	else
	    kernelTimerEventsDropped++;
	if (IsContinual(t->TI_type) && t->TI_interval) {
	    t->TI_due += t->TI_interval;
	    if (Due(t->TI_due, now))	/* fell behind: no burst */
		t->TI_due = now + t->TI_interval;
	    Insert(t);
	} else {
	    t->TI_next = endedList;
	    endedList = t;
	}
    }
}

/* CheckRealTimeTimers (timerInt.asm): inside ghal_crit */
void TimerCheckRealTime(dword now)
{
    if (!now)
	return;
    while (realTimeList && realTimeList->TI_due <= now) {
	KTimer *t = realTimeList;
	realTimeList = t->TI_next;
	if (t->TI_type == TIMER_ROUTINE_REAL_TIME) {
	    TimerRoutine *fn = (TimerRoutine *) (uintptr_t) t->TI_dest;
	    fn((word) t->TI_msg, t->TI_due);
	} else if (kernelTimerEventHook)
	    kernelTimerEventHook(t->TI_dest, t->TI_msg, t->TI_due, 0);
	else
	    kernelTimerEventsDropped++;
	t->TI_next = endedList;
	endedList = t;
    }
}

/* Block the running thread for `ticks' (timerList.asm): a timed wait on
 * a semaphore nobody signals. */
void TimerSleep(word ticks)
{
    KSem s;

    TimerReap();
    if (ticks == 0) {
	KernelYield();
	return;
    }
    KSemInit(&s, 0);
    KSemP(&s, ticks);
}

dword TimerGetCount(void)
{
    KernelPreemptPoint();
    return KernelTicks();
}
