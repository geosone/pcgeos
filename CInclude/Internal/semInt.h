/*
 * semInt.h -- a semaphore in a structure of one's own (Port32; the C
 * form of the Semaphore of Include/Internal/semInt.def).
 *
 * The asm put a Semaphore (value, queue of the threads waiting on it)
 * into its own data and used the PSem/VSem macros on it with interrupts
 * off.  In Port32 the kernel does it: the structure is the kernel's
 * KSem (Library/Kernel/kernelC.h), the routines are exported to
 * drivers.  Interrupts off is ghal_crit_enter/ghal_crit_leave
 * (ghal.h); the routines may be called inside such a section.
 */
#ifndef SEMINT_H
#define SEMINT_H

#include <geos.h>
#include <sem.h>

typedef struct {
    sword	Sem_value;	/* > 0: units left; < 0: threads waiting */
    Handle	Sem_queue;	/* the threads waiting, best first */
} Semaphore;

#define SEM_FOREVER	0xffffffffu	/* KSemP: wait for good */

#ifndef KERNELC_H	/* (the kernel declares them on its KSem) */
extern void KSemInit(Semaphore *s, sword value);
/* P: timeout in ticks, 0 does not block (PTimedSem with 0),
 * SEM_FOREVER waits; SE_TIMEOUT if it timed out */
extern SemaphoreError KSemP(Semaphore *s, dword timeout);
/* V: one unit more, the best thread waiting woken */
extern void KSemV(Semaphore *s);
/* n units more at once, as many threads woken as wait for them */
extern void KSemAdd(Semaphore *s, sword n);
/* every thread waiting woken (VAllSem) */
extern void KSemVAll(Semaphore *s);
#endif

#endif /* SEMINT_H */
