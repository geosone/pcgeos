/*
 * timerInt.c -- the timer interrupt of the C kernel (after timerInt.asm).
 *
 * GHAL_IRQ_TICK at 60 Hz (the PIT on dos32, a host thread elsewhere)
 * runs, as interrupt code inside ghal_crit: the due timers, the CPU
 * usage and time slice of the running thread and the timeouts of
 * blocked threads; every second it halves the CPU usage of all threads
 * and checks the real-time timers.
 * A thread switch it wants (time slice over, a better thread ready)
 * happens at the next kernel entry (KernelPreemptPoint) or, when every
 * thread waits, right away (the idle loop sees the ready thread).
 */
#include "kernelC.h"
#include "timerVariable.h"
#include "ghal.h"

static ghal_irq_source *tickSource;

static void TimerInterrupt(void *ctx)
{
    (void) ctx;
    TimerCheckList(KernelTicks());
    ThreadTick();
    if (--decayCount == 0) {
	decayCount = TIMER_INTERRUPT_RATE;
	ThreadDecay();
	CheckRealTimeTimers(KernelRealTimeNow(KernelTicks()));
    }
}

void KernelTimerInit(void)
{
    if (tickSource)
	return;
    KernelTicks();			/* start the clock */
    TimerReap();			/* the spares for interrupt code */
    ghal_irq_set_tick_rate(TIMER_INTERRUPT_RATE);
    if (ghal_irq_attach(GHAL_IRQ_TICK, TimerInterrupt, NULL, &tickSource)
	!= GHAL_OK)
	ghal_log(GHAL_LOG_ERROR, "kernel: no timer interrupt");
}

void KernelTimerExit(void)
{
    if (tickSource) {
	ghal_irq_detach(tickSource);
	tickSource = NULL;
    }
    TimerReap();
}

/* CheckRealTimeTimers (timerInt.asm): inside ghal_crit */
void CheckRealTimeTimers(dword now)
{
    if (!now)
	return;
    while (realTimeListPtr && realTimeListPtr->TI_due <= now) {
	KTimer *t = realTimeListPtr;
	realTimeListPtr = t->TI_next;
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
