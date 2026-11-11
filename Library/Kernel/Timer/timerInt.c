/*
 * timerInt.c -- the timer interrupt of the C kernel (after timerInt.asm).
 *
 * GHAL_IRQ_TICK at 60 Hz (the PIT on dos32, a host thread elsewhere)
 * runs, as interrupt code inside ghal_crit: the due timers, the CPU
 * usage and time slice of the running thread and the timeouts of
 * blocked threads; every second it halves the CPU usage of all threads.
 * A thread switch it wants (time slice over, a better thread ready)
 * happens at the next kernel entry (KernelPreemptPoint) or, when every
 * thread waits, right away (the idle loop sees the ready thread).
 */
#include "kernelC.h"
#include "ghal.h"

static ghal_irq_source *tickSource;
static dword ticksToDecay = TIMER_INTERRUPT_RATE;

static void TimerInterrupt(void *ctx)
{
    (void) ctx;
    TimerCheckList(KernelTicks());
    ThreadTick();
    if (--ticksToDecay == 0) {
	ticksToDecay = TIMER_INTERRUPT_RATE;
	ThreadDecay();
    }
}

void KernelTimerInit(void)
{
    if (tickSource)
	return;
    KernelTicks();			/* start the clock */
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
