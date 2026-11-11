/*
 * objectUtils.c -- master parts, chunk flags, in-use and interactible
 * counts, the block output (after objectUtils.asm).
 *
 * Port32 layout of an object chunk (Port32/ABI.md section 13):
 *
 *   base structure   class pointer, one word per master level (packed)
 *   master parts     each at an offset aligned to 8, taking its instance
 *                    size rounded up to 8; in level order
 *   variable data    from the next offset aligned to 8
 *
 * so the pointers and optrs in parts and variable data are aligned on
 * every host.  goc -cg lays out the objects of a resource the same way
 * (GOCR_PART).
 */
#include "kernelC.h"

static byte *Obj(optr o)
{
    return LMemDeref(o);
}

static word *MasterWord(optr o, word masterOffset)
{
    return (word *) (Obj(o) + masterOffset);
}

/*
 * Resize the master part whose offset word is at `masterOffset' to
 * `size' bytes, building it if it is not there (at the place of the next
 * part that is built, or after the base structure); the later parts and
 * the variable data move.
 */
void ObjResizeMaster(optr o, word masterOffset, word size)
{
    ClassStruct *cls = ObjClassOf(o);
    word last = cls->Class_masterOffset;
    word start, end, cur;
    sword delta;

    start = *MasterWord(o, masterOffset);
    if (start && masterOffset >= last) {
	/* the last part: the class's instance size */
	end = (word) OBJ_ALIGN(start + cls->Class_instanceSize);
    } else if (start) {
	/* up to the next part, which is built (bottom up) */
	word next = masterOffset;
	end = 0;
	while (!end && next < last) {
	    next += sizeof(word);
	    end = *MasterWord(o, next);
	}
	if (!end)
	    KernelFatal(OBJ_BAD_MASTER_PARTS);
    } else {
	/* not built: it goes where the next built part starts, or after
	 * the base structure (parts are built from the last level up) */
	word next = masterOffset;
	end = 0;
	while (!end && next < last) {
	    next += sizeof(word);
	    end = *MasterWord(o, next);
	}
	if (!end) {
	    word chunkSize = LMemGetChunkSize(o);
	    end = (word) OBJ_ALIGN(last + sizeof(word));
	    if (end > chunkSize) {
		/* the base structure alone: pad it to the parts */
		if (LMemInsertAt(o, chunkSize, (word) (end - chunkSize)))
		    KernelFatal(LMEM_CANNOT_ALLOCATE);
	    }
	}
	start = end;
	*MasterWord(o, masterOffset) = start;
    }
    cur = (word) (end - start);
    delta = (sword) (OBJ_ALIGN(size) - cur);
    if (delta == 0)
	return;
    if (delta > 0) {
	if (LMemInsertAt(o, end, (word) delta))
	    KernelFatal(LMEM_CANNOT_ALLOCATE);
    } else {
	LMemDeleteAt(o, (word) (end + delta), (word) -delta);
    }
    /* the parts after this one moved */
    for (cur = (word) (masterOffset + sizeof(word)); cur <= last;
	 cur += sizeof(word))
	if (*MasterWord(o, cur))
	    *MasterWord(o, cur) = (word) (*MasterWord(o, cur) + delta);
}

/* Build the master parts of an object down to (and including) the one
 * whose offset word is at `masterOffset'. */
void ObjInitializePart(optr obj, word masterOffset)
{
    ClassStruct *cls;

    MemLock(OptrToHandle(obj));
    cls = ObjClassOf(obj);
    for (;;) {
	if (!*MasterWord(obj, cls->Class_masterOffset))
	    ObjInitializeMaster(obj, cls);
	if (cls->Class_masterOffset == masterOffset)
	    break;
	/* up to the master class of this level, then above it */
	while (!(cls->Class_flags & CLASSF_MASTER_CLASS))
	    cls = cls->Class_superClass;
	if (!cls->Class_superClass)
	    KernelFatal(OBJ_BAD_MASTER_PARTS);
	cls = cls->Class_superClass == OBJ_VARIANT_SUPER
	      ? ObjResolveVariant(obj, cls) : cls->Class_superClass;
    }
    MemUnlock(OptrToHandle(obj));
}

/* ---------------------------------------------------------------------
 * Chunk flags (handle 0 of an object block: a byte per handle)
 * ------------------------------------------------------------------- */

static byte *FlagsOf(optr o)
{
    LMemBlockHeader *h = MemDeref(OptrToHandle(o));
    ChunkHandle ch = OptrToChunk(o);
    byte *flags;

    if (!h || !(h->LMBH_flags & LMF_HAS_FLAGS))
	KernelFatal(NOT_OBJECT_BLOCK);
    flags = LMemDeref(ConstructOptr(OptrToHandle(o), h->LMBH_offset));
    return flags + (ch - h->LMBH_offset) / 2;
}

ObjChunkFlags ObjGetFlags(optr o)
{
    return *FlagsOf(o);
}

/* a block without chunk flags (an LMem block that is not an object
 * block) has nothing to set, as in the asm kernel */
void ObjSetFlags(optr o, ObjChunkFlags bitsToSet, ObjChunkFlags bitsToClear)
{
    LMemBlockHeader *h = MemDeref(OptrToHandle(o));
    byte *f;

    if (h && !(h->LMBH_flags & LMF_HAS_FLAGS))
	return;
    f = FlagsOf(o);

    *f = (byte) ((*f & ~bitsToClear) | bitsToSet);
    /* VM object blocks: dirtying the VM block comes with VMem */
}

void ObjMarkDirty(optr o)
{
    ObjSetFlags(o, OCF_DIRTY, 0);
}

/* ---------------------------------------------------------------------
 * In-use and interactible counts, the block output
 * ------------------------------------------------------------------- */

static ObjLMemBlockHeader *Header(MemHandle mh)
{
    ObjLMemBlockHeader *h = MemDeref(mh);

    if (!h || h->OLMBH_header.LMBH_lmemType != LMEM_TYPE_OBJ_BLOCK)
	KernelFatal(NOT_OBJECT_BLOCK);
    return h;
}

void ObjIncInUseCount(MemHandle mh)
{
    Header(mh)->OLMBH_inUseCount++;
}

void ObjDecInUseCount(MemHandle mh)
{
    ObjLMemBlockHeader *h = Header(mh);

    if (--h->OLMBH_inUseCount == 0 &&
	(h->OLMBH_header.LMBH_flags & LMF_AUTO_FREE)) {
	h->OLMBH_header.LMBH_flags &= ~LMF_AUTO_FREE;
	ObjFreeObjBlock(mh);
    }
}

/* tell the block's output (MSG_META_NOTIFY_OBJ_BLOCK_[NOT_]INTERACTIBLE) */
static void SendToOutput(MemHandle mh, Message msg)
{
    optr out = Header(mh)->OLMBH_output;

    if (OptrToHandle(out))
	CObjMessageP(0, out, msg, &mh, sizeof(mh), NULL, 0);
}

void ObjIncInteractibleCount(MemHandle mh)
{
    if (Header(mh)->OLMBH_interactibleCount++ == 0) {
	ObjIncInUseCount(mh);
	SendToOutput(mh, KMSG_META_NOTIFY_OBJ_BLOCK_INTERACTIBLE);
    }
}

void ObjDecInteractibleCount(MemHandle mh)
{
    if (--Header(mh)->OLMBH_interactibleCount == 0) {
	SendToOutput(mh, KMSG_META_NOTIFY_OBJ_BLOCK_NOT_INTERACTIBLE);
	ObjDecInUseCount(mh);
    }
}

Boolean ObjTestIfObjBlockRunByCurThread(MemHandle mh)
{
    return (ThreadHandle) MemGetInfo(mh, MGIT_EXEC_THREAD) ==
	   KernelThisThread();
}

void ObjBlockSetOutput(MemHandle mh, optr o)
{
    MemLock(mh);
    Header(mh)->OLMBH_output = o;
    MemUnlock(mh);
}

optr ObjBlockGetOutput(MemHandle mh)
{
    optr o;

    MemLock(mh);
    o = Header(mh)->OLMBH_output;
    MemUnlock(mh);
    return o;
}

void *ObjLockObjBlock(MemHandle mh)
{
    void *p = MemLock(mh);

    Header(mh);
    return p;
}
