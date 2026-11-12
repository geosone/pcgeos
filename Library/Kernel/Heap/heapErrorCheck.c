/*
 * heapErrorCheck.c -- the error checks geodes call (after
 * heapErrorCheck.asm, geodesErrorCheck.asm and the ECCHECK* stubs of
 * heapC.asm), and CFatalError and CWarningNotice of bootBoot.asm.
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

static void CheckType(Handle h, byte type, Boolean nullOk)
{
    HandleEntry *he;

    if (!h && nullOk)
	return;
    he = HandleEntryPeek(h);
    if (!he || he->HE_type != type)
	KernelFatal(EC_BAD_HANDLE);
}

void ECCheckMemHandle(MemHandle mh)	    { CheckType(mh, HT_MEM, FALSE); }
void ECCheckMemHandleNS(MemHandle mh)	    { CheckType(mh, HT_MEM, FALSE); }
void ECCheckResourceHandle(MemHandle mh)    { CheckType(mh, HT_MEM, FALSE); }
void ECCheckLMemHandle(MemHandle mh)	    { CheckType(mh, HT_MEM, FALSE); }
void ECCheckLMemHandleNS(MemHandle mh)	    { CheckType(mh, HT_MEM, FALSE); }
void ECCheckGStateHandle(GStateHandle h)    { CheckType(h, HT_MEM, FALSE); }
void ECCheckWindowHandle(WindowHandle h)    { CheckType(h, HT_MEM, FALSE); }
void ECCheckGeodeHandle(GeodeHandle gh)	    { CheckType(gh, HT_MEM, FALSE); }
void ECCheckProcessHandle(GeodeHandle gh)   { CheckType(gh, HT_MEM, FALSE); }
void ECCheckDriverHandle(GeodeHandle gh)    { CheckType(gh, HT_MEM, FALSE); }
void ECCheckLibraryHandle(GeodeHandle gh)   { CheckType(gh, HT_MEM, FALSE); }
void ECCheckFileHandle(FileHandle fh)	    { CheckType(fh, SIG_FILE, FALSE); }
void ECCheckThreadHandle(ThreadHandle th)   { CheckType(th, SIG_THREAD, FALSE); }
void ECCheckQueueHandle(QueueHandle qh)	    { CheckType(qh, SIG_QUEUE, FALSE); }

void ECCheckBounds(void *address)	    { (void) address; }
void ECCheckStack(void)			    { }

void CFatalError(word code)
{
    ghal_log(GHAL_LOG_ERROR, "CFatalError %u", (unsigned) code);
    KernelFatal(C_FATAL_ERROR);
}

void CWarningNotice(word code)
{
    ghal_log(GHAL_LOG_WARN, "CWarningNotice %u", (unsigned) code);
}
