/*
 * processLockQueue.c -- locking the print queue (the C port of
 * processLockQueue.asm).
 *
 * The print queue is shared by the spooler's threads and every thread
 * that calls the library routines.  queueSemaphore guards queueHandle
 * and the block; a caller holds it from LockQueue to UnlockQueue, even
 * when LockQueue finds no queue (the asm's callers then release it
 * themselves, as these do with UnlockQueue).
 */
#include "spoolLocal.h"

/* LockQueue: TRUE (the asm's carry) if there is no queue */
Boolean LockQueue(void)
{
    ThreadPSem(queueSemaphore);
    if (!queueHandle)
	return TRUE;
    MemPLock(queueHandle);
    return FALSE;
}

void UnlockQueue(void)
{
    if (queueHandle)
	MemUnlockV(queueHandle);
    ThreadVSem(queueSemaphore);
}
