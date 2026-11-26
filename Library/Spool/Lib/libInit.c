/*
 * libInit.c -- after libInit.asm: This file contains code to
 * initialize the spool library
 */
#include <geos.h>
#include <heap.h>
#include <geode.h>
#include <library.h>
#include "spoolLocal.h"

/* the library entry: the semaphores of the queue and the error box
 * (static Semaphores in the asm's idata) */
Boolean SpoolLibraryEntry(LibraryCallType ty, GeodeHandle client)
{
    (void) client;
    if (ty == LCT_ATTACH) {
	queueSemaphore = ThreadAllocSem(1);
	errorThreadLock = ThreadAllocThreadLock();
	HandleModifyOwner(queueSemaphore, GeodeGetCodeProcessHandle());
	HandleModifyOwner(errorThreadLock, GeodeGetCodeProcessHandle());
    }
    return FALSE;
}
