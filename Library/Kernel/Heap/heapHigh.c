/*
 * heapHigh.c -- the memory block API of the C kernel (CInclude/heap.h;
 * after heapHigh.asm and heapC.asm).
 *
 * Every block is one GHAL memory block (ghal_mem_alloc), as in the PM
 * kernel's GPMI_CALL_ALLOCATE_BLOCK: a block never moves while it exists,
 * except that MemReAlloc may move it (as it could change the segment of
 * a block in the asm kernel).  There is no compaction and no swapping;
 * "discarding" frees the data and keeps the handle, and MemLock on a
 * discarded block returns NULL (the caller reloads it, e.g. a resource).
 * Block sizes stay words (64K at most), as LMem chunk handles are 16 bits.
 *
 * HandleP/HandleV use the KSem of the handle (threadSem.c); MemThreadGrab
 * keeps the grabbing thread in the other info, as the asm kernel does.
 *
 * Shared and exclusive locks (MemLockShared, MemLockExcl): the handle's
 * semaphore is held by one writer or by the group of sharers, whose
 * number is in HE_nesting.  A sharer joins a group that holds it; else it
 * takes a free semaphore, or waits.  Waiting sharers (of any block) wait
 * on one queue and all look again when a block is released or downgraded
 * (the asm woke the readers on the handle's queue); writers wait on the
 * handle's semaphore.  Port32/PLAN.md P4.
 */
#include "kernelC.h"
#include "ghal.h"

#include <string.h>

GeodeHandle kernelCurrentGeode;

/* flags a caller may give (HF_SWAPPED is the heap's own business) */
#define HF_ALLOC_MASK	(HF_FIXED | HF_SHARABLE | HF_DISCARDABLE | \
			 HF_SWAPABLE | HF_LMEM | HF_DISCARDED)
#define HF_MODIFY_MASK	(HF_SHARABLE | HF_DISCARDABLE | HF_SWAPABLE | HF_LMEM)
#define HAF_KEEP_MASK	(HAF_UI | HAF_READ_ONLY | HAF_OBJECT_RESOURCE | \
			 HAF_CODE | HAF_CONFORMING)

static MemHandle AllocFailed(HeapAllocFlags haf)
{
    if (haf & HAF_NO_ERR)
	KernelFatal(HAF_NO_ERR_ALLOC_FAILED);
    return 0;
}

MemHandle MemAllocSetOwner(GeodeHandle owner, word byteSize,
			   HeapFlags hf, HeapAllocFlags haf)
{
    MemHandle h;
    HandleEntry *he;
    void *data = NULL;

    EC_CHECK(!(hf & ~HF_ALLOC_MASK), ALLOC_BAD_FLAGS);
    EC_CHECK(byteSize != 0 || (hf & HF_DISCARDED), ALLOC_ZERO);
    if (!(hf & HF_DISCARDED)) {
	data = ghal_mem_alloc(byteSize ? byteSize : 1,
			      (haf & HAF_ZERO_INIT) ? GHAL_MEM_ZERO : 0);
	if (!data)
	    return AllocFailed(haf);
    }
    h = HandleAlloc(HT_MEM, owner);
    if (!h) {
	ghal_mem_free(data);
	if (haf & HAF_NO_ERR)
	    KernelFatal(HANDLE_TABLE_FULL);
	return 0;
    }
    he = HandleEntryOf(h);
    he->HE_flags = hf;
    he->HE_allocFlags = haf & HAF_KEEP_MASK;
    he->HE_size = byteSize;
    he->HE_addr = data;
    KSemInit(&he->HE_sem, 1);
    if ((haf & HAF_LOCK) && data && !(hf & HF_FIXED))
	he->HE_lockCount = 1;
    return h;
}

MemHandle MemAlloc(word byteSize, HeapFlags hf, HeapAllocFlags haf)
{
    return MemAllocSetOwner(kernelCurrentGeode, byteSize, hf, haf);
}

MemHandle MemReAlloc(MemHandle mh, word byteSize, HeapAllocFlags haf)
{
    HandleEntry *he = MemEntryOf(mh);
    void *data;

    if (byteSize == 0) {
	/* the data goes away, the handle stays (discarded) */
	ghal_mem_free(he->HE_addr);
	he->HE_addr = NULL;
	he->HE_size = 0;
	he->HE_flags |= HF_DISCARDED;
	return mh;
    }
    if (!he->HE_addr) {
	data = ghal_mem_alloc(byteSize,
			      (haf & HAF_ZERO_INIT) ? GHAL_MEM_ZERO : 0);
	if (!data)
	    return AllocFailed(haf);
	he->HE_flags &= (byte) ~HF_DISCARDED;
    } else {
	data = ghal_mem_resize(he->HE_addr, byteSize);
	if (!data)
	    return AllocFailed(haf);
	if ((haf & HAF_ZERO_INIT) && byteSize > he->HE_size)
	    memset((byte *) data + he->HE_size, 0, byteSize - he->HE_size);
    }
    he->HE_addr = data;
    he->HE_size = byteSize;
    if ((haf & HAF_LOCK) && !(he->HE_flags & HF_FIXED)) {
	EC_CHECK(he->HE_lockCount < 255, TOO_MANY_LOCKS);
	he->HE_lockCount++;
    }
    return mh;
}

void MemFree(MemHandle mh)
{
    HandleEntry *he = MemEntryOf(mh);

    ghal_mem_free(he->HE_addr);
    HandleFree(mh);
}

void MemDiscard(MemHandle mh)
{
    HandleEntry *he = MemEntryOf(mh);

    if (he->HE_lockCount != 0 || !he->HE_addr)
	return;
    ghal_mem_free(he->HE_addr);
    he->HE_addr = NULL;
    he->HE_flags |= HF_DISCARDED;
}

dword MemGetInfo(MemHandle mh, MemGetInfoType info)
{
    HandleEntry *he = MemEntryOf(mh);

    switch (info) {
    case MGIT_SIZE:
	return he->HE_size;
    case MGIT_FLAGS_AND_LOCK_COUNT:
	return (dword) he->HE_flags | ((dword) he->HE_lockCount << 8);
    case MGIT_OWNER_OR_VM_FILE_HANDLE:
	return he->HE_owner;
    case MGIT_OTHER_INFO:
    case MGIT_EXEC_THREAD:
	return he->HE_otherInfo;
    default:
	break;
    }
    KernelFatal(MEM_GET_INFO_BAD_PARAMETER);
    return 0;				/* not reached */
}

void MemModifyFlags(MemHandle mh, HeapFlags bitsToSet, HeapFlags bitsToClear)
{
    HandleEntry *he = MemEntryOf(mh);

    EC_CHECK(!((bitsToSet | bitsToClear) & ~HF_MODIFY_MASK), ALLOC_BAD_FLAGS);
    he->HE_flags = (byte) ((he->HE_flags | bitsToSet) & ~bitsToClear);
}

void HandleModifyOwner(Handle h, GeodeHandle owner)
{
    HandleEntryOf(h)->HE_owner = owner;
}

void MemModifyOtherInfo(MemHandle mh, dword otherInfo)
{
    MemEntryOf(mh)->HE_otherInfo = otherInfo;
}

void *MemLock(MemHandle mh)
{
    HandleEntry *he = MemEntryOf(mh);

    KernelPreemptPoint();
    if (!he->HE_addr)
	return NULL;			/* discarded */
    if (!(he->HE_flags & HF_FIXED)) {
	if (he->HE_lockCount == 255)
	    KernelFatal(TOO_MANY_LOCKS);
	he->HE_lockCount++;
    }
    return he->HE_addr;
}

void MemUnlock(MemHandle mh)
{
    HandleEntry *he = MemEntryOf(mh);

    if (he->HE_flags & HF_FIXED)
	return;
    if (he->HE_lockCount == 0)
	KernelFatal(BAD_UNLOCK);
    he->HE_lockCount--;
}

void *MemDeref(MemHandle mh)
{
    return MemEntryOf(mh)->HE_addr;
}

/* the owner of a VM file's block is the owner of the file (MemOwner of
 * heapHigh.asm) */
GeodeHandle MemOwner(MemHandle mh)
{
    Handle owner = HandleEntryOf(mh)->HE_owner;
    HandleEntry *he = HandleEntryPeek(owner);

    return he && he->HE_type == SIG_VM ? he->HE_owner : owner;
}

typedef struct {
    const byte *ptr;
    MemHandle found;
} PtrSearch;

static int FindPtr(Handle h, HandleEntry *he, void *ctx)
{
    PtrSearch *s = ctx;
    const byte *start = he->HE_addr;

    if (he->HE_type == HT_MEM && start && s->ptr >= start &&
	s->ptr < start + (he->HE_size ? he->HE_size : 1)) {
	s->found = h;
	return 1;
    }
    return 0;
}

MemHandle MemPtrToHandle(void *ptr)
{
    PtrSearch s;

    s.ptr = ptr;
    s.found = 0;
    HandleEnum(FindPtr, &s);
    return s.found;
}

void MemInitRefCount(MemHandle mh, word count)
{
    EC_CHECK(count != 0, MEM_REFERENCE_COUNT_ILLEGAL_INITIAL_VALUE);
    MemEntryOf(mh)->HE_otherInfo = count;
}

void MemIncRefCount(MemHandle mh)
{
    HandleEntry *he = MemEntryOf(mh);

    if (he->HE_otherInfo == 0xffff)
	KernelFatal(MEM_REFERENCE_COUNT_OVERFLOW);
    he->HE_otherInfo++;
}

void MemDecRefCount(MemHandle mh)
{
    HandleEntry *he = MemEntryOf(mh);

    if (he->HE_otherInfo == 0)
	KernelFatal(MEM_REFERENCE_COUNT_UNDERFLOW);
    if (--he->HE_otherInfo == 0)
	MemFree(mh);
}

/* ---------------------------------------------------------------------
 * Exclusive access between threads
 * ------------------------------------------------------------------- */

void HandleP(MemHandle mh)
{
    KSemP(&HandleEntryOf(mh)->HE_sem, KSEM_FOREVER);
}

void HandleV(MemHandle mh)
{
    KSemV(&HandleEntryOf(mh)->HE_sem);
}

void *MemPLock(MemHandle mh)
{
    HandleP(mh);
    return MemLock(mh);
}

void MemUnlockV(MemHandle mh)
{
    MemUnlock(mh);
    HandleV(mh);
}

/* grab for the running thread; `block' 0: return BLOCK_GRABBED instead
 * of waiting */
static void *Grab(MemHandle mh, int block)
{
    HandleEntry *he = MemEntryOf(mh);
    ThreadHandle cur = KernelThisThread();

    if (he->HE_nesting && he->HE_otherInfo == cur) {
	if (he->HE_nesting == 0xffff)
	    KernelFatal(TOO_MANY_LOCKS);
	he->HE_nesting++;
    } else {
	if (KSemP(&he->HE_sem, block ? KSEM_FOREVER : 0) != SE_NO_ERROR)
	    return (void *) BLOCK_GRABBED;
	he->HE_otherInfo = cur;
	he->HE_nesting = 1;
    }
    return MemLock(mh);
}

void *MemThreadGrab(MemHandle mh)
{
    return Grab(mh, 1);
}

void *MemThreadGrabNB(MemHandle mh)
{
    return Grab(mh, 0);
}

void MemThreadRelease(MemHandle mh)
{
    HandleEntry *he = MemEntryOf(mh);

    if (he->HE_nesting == 0 || he->HE_otherInfo != KernelThisThread())
	KernelFatal(THREAD_RELEASE_NOT_OWNER);
    MemUnlock(mh);
    if (--he->HE_nesting == 0) {
	he->HE_otherInfo = 0;
	KSemV(&he->HE_sem);
    }
}

/* ---------------------------------------------------------------------
 * Shared and exclusive locks (heapHigh.asm)
 * ------------------------------------------------------------------- */

static KSem sharersQueue = { 0, 0 };	/* only its queue is used */

/* inside ghal_crit: every waiting sharer looks again */
static void WakeSharers(void)
{
    while (sharersQueue.S_queue)
	ThreadWakeOne(&sharersQueue);
}

void *MemLockShared(MemHandle mh)
{
    HandleEntry *he = MemEntryOf(mh);

    KernelPreemptPoint();
    ghal_crit_enter();
    for (;;) {
	if (he->HE_nesting) {		/* sharers hold it: join them */
	    he->HE_nesting++;
	    break;
	}
	if (he->HE_sem.S_value > 0) {	/* free */
	    he->HE_sem.S_value--;
	    he->HE_nesting = 1;
	    break;
	}
	(void) ThreadBlockOn(&sharersQueue, KSEM_FOREVER);
    }
    ghal_crit_leave();
    return MemLock(mh);
}

void *MemLockExcl(MemHandle mh)
{
    (void) KSemP(&MemEntryOf(mh)->HE_sem, KSEM_FOREVER);
    return MemLock(mh);
}

/* MemUnlockShared, and MemUnlockExcl (heap.h makes it the same) */
void MemUnlockShared(MemHandle mh)
{
    HandleEntry *he = MemEntryOf(mh);

    MemUnlock(mh);
    ghal_crit_enter();
    if (he->HE_nesting && --he->HE_nesting) {
	ghal_crit_leave();		/* other sharers are left */
	return;
    }
    if (++he->HE_sem.S_value <= 0)
	ThreadWakeOne(&he->HE_sem);	/* a writer gets it */
    WakeSharers();
    ghal_crit_leave();
}

/* a shared lock (this thread's only one) to an exclusive one: at once if
 * no one else shares the block, else after waiting */
void *MemUpgradeSharedLock(MemHandle mh)
{
    HandleEntry *he = MemEntryOf(mh);

    EC_CHECK(he->HE_nesting != 0,
	     UPGRADING_SHARED_LOCK_WITHOUT_HAVING_LOCKED_IT_SHARED);
    ghal_crit_enter();
    if (he->HE_nesting == 1) {
	he->HE_nesting = 0;		/* the semaphore stays ours */
	ghal_crit_leave();
	return MemDeref(mh);
    }
    ghal_crit_leave();
    MemUnlockShared(mh);
    return MemLockExcl(mh);
}

void MemDowngradeExclLock(MemHandle mh)
{
    HandleEntry *he = MemEntryOf(mh);

    EC_CHECK(he->HE_nesting == 0 && he->HE_sem.S_value <= 0,
	     DOWNGRADING_EXCL_LOCK_WITHOUT_HAVING_LOCKED_IT_EXCL);
    ghal_crit_enter();
    he->HE_nesting = 1;
    WakeSharers();
    ghal_crit_leave();
}

/* Port32: there are no virtual segments; a pointer is the data itself */
void *MemLockFixedOrMovable(void *ptr)
{
    return ptr;
}

void MemUnlockFixedOrMovable(void *ptr)
{
    (void) ptr;
}
