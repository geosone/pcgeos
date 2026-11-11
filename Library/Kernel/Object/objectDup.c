/*
 * objectDup.c -- duplicating resources and the saved-block list (after
 * objectDup.asm and GeodeDuplicateResource of geodesResource.asm).
 *
 * A resource is duplicated from the module's data (the GocResource that
 * goc -cg made), as the asm kernel read it from the geode file: the
 * source block itself is not used, so it may be in memory.  The copy is
 * relocated as a resource is when it is loaded, with two differences:
 *
 *  - references to the template resource itself lead to the copy
 *    (OLMBH_resource, objectReloc.c; glue's ORS_CURRENT_BLOCK);
 *  - other resources are those of the template's owner, unless the new
 *    owner runs the same geode (another instance of the same
 *    application): then they are the new owner's.
 *
 * A duplicated block of a detachable resource, and a block given to
 * ObjSaveBlock, goes onto the saved-block list of its owner (a
 * SIG_SAVED_BLOCK handle per block: HE_otherInfo is the block, HE_size
 * the VM ID, HE_next the next one), which saving to state will use.
 * Not yet: the state file (VM blocks for the saved blocks).
 */
#include "kernelC.h"
#include "geosmod.h"
#include <vm.h>

#include <string.h>

/* ---------------------------------------------------------------------
 * The saved-block list
 * ------------------------------------------------------------------- */

static void AddToSavedList(MemHandle mh)
{
    GeodeHandle owner = MemOwner(mh);
    GeodeCore *c;
    Handle sb;
    word id = 1;

    GeodeLock();
    c = GeodeCoreOf(owner);
    if (c) {
	sb = HandleAlloc(SIG_SAVED_BLOCK, owner);
	if (!sb)
	    KernelFatal(HANDLE_TABLE_FULL);
	/* the next odd ID (the asm kernel's, without a state file) */
	if (c->GC_savedBlocks)
	    id = (word) (HandleEntryOf(c->GC_savedBlocks)->HE_size + 2);
	HandleEntryOf(sb)->HE_otherInfo = mh;
	HandleEntryOf(sb)->HE_size = id;
	HandleEntryOf(sb)->HE_next = c->GC_savedBlocks;
	c->GC_savedBlocks = sb;
    }
    GeodeUnlock();
    ((LMemBlockHeader *) MemLock(mh))->LMBH_flags |= LMF_DETACHABLE;
    MemUnlock(mh);
}

static void RemoveFromSavedList(MemHandle mh)
{
    GeodeCore *c;
    Handle *p;

    GeodeLock();
    c = GeodeCoreOf(MemOwner(mh));
    if (c)
	for (p = &c->GC_savedBlocks; *p; p = &HandleEntryOf(*p)->HE_next)
	    if (HandleEntryOf(*p)->HE_otherInfo == mh) {
		Handle sb = *p;
		*p = HandleEntryOf(sb)->HE_next;
		HandleFree(sb);
		break;
	    }
    GeodeUnlock();
}

void ObjSaveBlock(MemHandle mh)
{
    AddToSavedList(mh);
    /* there is no other copy of the block in the state file */
    ((LMemBlockHeader *) MemLock(mh))->LMBH_flags |= LMF_DUPLICATED;
    MemUnlock(mh);
}

/* ---------------------------------------------------------------------
 * Duplicating
 * ------------------------------------------------------------------- */

/* the module data of a resource handle, and its number and flags */
static const GocResource *ResourceOf(MemHandle mh, word *resf)
{
    GeodeHandle owner = MemOwner(mh);
    sdword res = GeodeHandleToResource(owner, mh);
    const GocResource *r;

    if (res < 0 || res == GOCR_PROCESS_RES)
	KernelFatal(OBJ_DUPLICATE_NOT_A_RESOURCE);
    r = GeodeResourceImage(owner, (word) res, resf);
    if (!r)
	KernelFatal(OBJ_DUPLICATE_NOT_A_RESOURCE);
    return r;
}

static void DupFlags(word resf, HeapFlags *hf, HeapAllocFlags *haf)
{
    /* a copy cannot be discarded: it is not in the geode */
    *hf = HF_SWAPABLE;
    if (resf & RESF_SHARED)
	*hf |= HF_SHARABLE;
    *haf = 0;
    if (resf & RESF_UI)
	*haf |= HAF_UI;
    if (resf & RESF_OBJECT)
	*haf |= HAF_OBJECT_RESOURCE;
}

/* a new block with the contents of resource `mh' as the geode holds
 * them, owned by the resource's owner, not relocated */
MemHandle GeodeDuplicateResource(MemHandle mh)
{
    word resf;
    const GocResource *r = ResourceOf(mh, &resf);
    HeapFlags hf;
    HeapAllocFlags haf;

    DupFlags(resf, &hf, &haf);
    return LMemCreateResourceBlock(r, MemOwner(mh), hf, haf);
}

/* the thread to run the copy (DetermineBurdenThreadForDuplicatedResource):
 * 0 the running thread; -1 the source's: its process's first thread
 * becomes the new owner's (there is no UI thread yet) */
static ThreadHandle BurdenThread(MemHandle src, GeodeHandle owner,
				 ThreadHandle burden)
{
    ThreadHandle t;

    if (!burden)
	return KernelThisThread();
    if (burden != (ThreadHandle) -1)
	return burden;
    t = (ThreadHandle) MemGetInfo(src, MGIT_OTHER_INFO);
    if (GeodeCoreOf(MemOwner(src)) &&
	t == (ThreadHandle) MemGetInfo(MemOwner(src), MGIT_OTHER_INFO) &&
	GeodeCoreOf(owner))
	t = (ThreadHandle) MemGetInfo(owner, MGIT_OTHER_INFO);
    return t;
}

/* both geodes run the same module (instances of one application) */
static Boolean SameGeode(GeodeHandle a, GeodeHandle b)
{
    GeodeCore *ca, *cb;
    Boolean same;

    GeodeLock();
    ca = GeodeCoreOf(a);
    cb = GeodeCoreOf(b);
    same = ca && cb && memcmp(ca->GC_name, cb->GC_name,
			      sizeof(ca->GC_name)) == 0;
    GeodeUnlock();
    return same;
}

MemHandle ObjDuplicateResource(MemHandle blockToDup, GeodeHandle owner,
			       ThreadHandle burdenThread)
{
    GeodeHandle srcOwner = MemOwner(blockToDup);
    word resf;
    const GocResource *r = ResourceOf(blockToDup, &resf);
    HeapFlags hf;
    HeapAllocFlags haf;
    MemHandle mh;
    ObjLMemBlockHeader *h;

    if (((const LMemBlockHeader *) r->GR_header)->LMBH_lmemType !=
	LMEM_TYPE_OBJ_BLOCK)
	KernelFatal(OBJ_DUPLICATE_BLOCK_REQUIRES_AN_OBJECT_BLOCK);
    if (!owner)
	owner = GeodeGetProcessHandle();
    else if (owner == (GeodeHandle) -1)
	owner = srcOwner;
    burdenThread = BurdenThread(blockToDup, owner, burdenThread);

    /* relocated for the template's owner, or the new one if it runs the
     * same geode */
    DupFlags(resf, &hf, &haf);
    mh = LMemCreateResourceBlock(r, SameGeode(srcOwner, owner) ? owner
							       : srcOwner,
				 hf, haf);
    if (!mh)
	return 0;
    h = MemLock(mh);
    h->OLMBH_header.LMBH_flags =
	(h->OLMBH_header.LMBH_flags | LMF_DUPLICATED) & ~LMF_IN_RESOURCE;
    h->OLMBH_resourceSize = h->OLMBH_header.LMBH_blockSize;
    MemUnlock(mh);
    MemModifyOtherInfo(mh, burdenThread);
    ObjRelocateResourceChunks(mh, r);
    ObjRelocateObjBlock(mh, VMRT_RELOCATE_FROM_RESOURCE);
    HandleEntryOf(mh)->HE_owner = owner;

    h = MemLock(mh);
    if (h->OLMBH_header.LMBH_flags & LMF_DETACHABLE) {
	MemUnlock(mh);
	AddToSavedList(mh);
    } else
	MemUnlock(mh);
    return mh;
}

void ObjFreeDuplicate(MemHandle mh)
{
    LMemBlockHeader *h = MemLock(mh);
    Boolean saved = (h->LMBH_flags & LMF_DETACHABLE) != 0;

    MemUnlock(mh);
    if (saved)
	RemoveFromSavedList(mh);
    MemFree(mh);
}
