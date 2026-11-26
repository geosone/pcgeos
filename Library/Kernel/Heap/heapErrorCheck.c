/*
 * heapErrorCheck.c -- the memory handle checks geodes call (after
 * heapErrorCheck.asm); the checks of other handles are beside the asm
 * they come from (geodesErrorCheck.c, fileEC.c, lmemErrorCheck.c,
 * threadErrorCheck.c, winWindows.c), all through ECCheckHandleType.
 *
 * Port32: a handle is checked for its type in the handle table: a
 * memory handle for the memory, GState, window, LMem, resource and
 * geode checks (they are blocks), a file, thread or queue handle by its
 * signature.  The checks of the contents (the LMem heap, the class of
 * an object) and of addresses (ECCheckBounds, ECCheckStack: the host
 * guards its memory and stacks) do nothing yet.  A failed check is fatal
 * (EC_BAD_HANDLE).
 */
#include "kernelC.h"
#include "ghal.h"

void ECCheckHandleType(Handle h, byte type, Boolean nullOk)
{
    HandleEntry *he;

    if (!h && nullOk)
	return;
    he = HandleEntryPeek(h);
    if (!he || he->HE_type != type)
	KernelFatal(EC_BAD_HANDLE);
}

void ECCheckMemHandle(MemHandle mh)	    { ECCheckHandleType(mh, HT_MEM, FALSE); }
void ECCheckMemHandleNS(MemHandle mh)	    { ECCheckHandleType(mh, HT_MEM, FALSE); }

void ECCheckBounds(void *address)	    { (void) address; }
