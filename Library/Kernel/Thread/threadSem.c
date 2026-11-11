/*
 * threadSem.c -- semaphores and thread locks of the C kernel (after
 * threadSem.asm).  A semaphore is a SIG_SEMAPHORE handle with a KSem
 * (value, queue of waiting threads); a thread lock is one with
 * HSF_THREAD_LOCK, the owning thread in HE_otherInfo and the nesting in
 * HE_nesting (ThreadLock of semInt.def).  The same KSem gives every
 * memory handle its HandleP/HandleV (heapHigh.c).
 */
#include "kernelC.h"
#include "ghal.h"

void KSemInit(KSem *s, sword value)
{
    s->S_value = value;
    s->S_queue = 0;
}

SemaphoreError KSemP(KSem *s, dword timeout)
{
    SemaphoreError r = SE_NO_ERROR;

    KernelPreemptPoint();
    ghal_crit_enter();
    if (--s->S_value < 0) {
	if (timeout == 0) {
	    s->S_value++;
	    r = SE_TIMEOUT;
	} else if (ThreadBlockOn(s, timeout))
	    r = SE_TIMEOUT;
    }
    ghal_crit_leave();
    return r;
}

void KSemV(KSem *s)
{
    ghal_crit_enter();
    if (++s->S_value <= 0)
	ThreadWakeOne(s);
    ghal_crit_leave();
}

static HandleEntry *SemOf(Handle h, byte lockFlag)
{
    HandleEntry *he = HandleEntryOf(h);

    if (he->HE_type != SIG_SEMAPHORE ||
	(he->HE_flags & HSF_THREAD_LOCK) != lockFlag)
	KernelFatal(NON_SEMAPHORE_PASSED_TO_SEM_ROUTINE);
    return he;
}

static Handle AllocSem(word value, byte flags)
{
    Handle h = HandleAlloc(SIG_SEMAPHORE, kernelCurrentGeode);
    HandleEntry *he;

    if (!h)
	KernelFatal(HANDLE_TABLE_FULL);
    he = HandleEntryOf(h);
    he->HE_flags = flags;
    KSemInit(&he->HE_sem, (sword) value);
    return h;
}

SemaphoreHandle ThreadAllocSem(word value)
{
    return AllocSem(value, 0);
}

void ThreadFreeSem(SemaphoreHandle sem)
{
    SemOf(sem, 0);
    HandleFree(sem);
}

SemaphoreError ThreadPSem(SemaphoreHandle sem)
{
    return KSemP(&SemOf(sem, 0)->HE_sem, KSEM_FOREVER);
}

void ThreadVSem(SemaphoreHandle sem)
{
    KSemV(&SemOf(sem, 0)->HE_sem);
}

SemaphoreError ThreadPTimedSem(SemaphoreHandle sem, word timeout)
{
    return KSemP(&SemOf(sem, 0)->HE_sem, timeout);
}

ThreadLockHandle ThreadAllocThreadLock(void)
{
    return AllocSem(1, HSF_THREAD_LOCK);
}

void ThreadFreeThreadLock(ThreadLockHandle sem)
{
    SemOf(sem, HSF_THREAD_LOCK);
    HandleFree(sem);
}

void ThreadGrabThreadLock(ThreadLockHandle sem)
{
    HandleEntry *he = SemOf(sem, HSF_THREAD_LOCK);
    ThreadHandle cur = KernelThisThread();

    if (he->HE_nesting && he->HE_otherInfo == cur) {
	he->HE_nesting++;
	return;
    }
    KSemP(&he->HE_sem, KSEM_FOREVER);
    he->HE_otherInfo = cur;
    he->HE_nesting = 1;
}

void ThreadReleaseThreadLock(ThreadLockHandle sem)
{
    HandleEntry *he = SemOf(sem, HSF_THREAD_LOCK);

    if (he->HE_nesting == 0 || he->HE_otherInfo != KernelThisThread())
	KernelFatal(UNLOCK_MODULE_BY_WRONG_THREAD);
    if (--he->HE_nesting == 0) {
	he->HE_otherInfo = 0;
	KSemV(&he->HE_sem);
    }
}
